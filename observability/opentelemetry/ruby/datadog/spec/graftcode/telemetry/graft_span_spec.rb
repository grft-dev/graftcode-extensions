# frozen_string_literal: true

require 'graftcode/telemetry/datadog'
require 'graftcode/instrumentation'

RSpec.describe 'Graftcode Datadog graft spans' do
  def fetch_command
    Hypertube::Utils::Command.new(
      Hypertube::Utils::RuntimeNameHt::JVM,
      Hypertube::Utils::CommandType::INVOKE_STATIC_METHOD,
      [
        Hypertube::Utils::Command.new(
          Hypertube::Utils::RuntimeNameHt::JVM,
          Hypertube::Utils::CommandType::GET_TYPE,
          [Hypertube::Utils::Command.new(Hypertube::Utils::RuntimeNameHt::JVM, Hypertube::Utils::CommandType::VALUE, ['SomeLib.Service'])]
        ),
        Hypertube::Utils::Command.new(Hypertube::Utils::RuntimeNameHt::JVM, Hypertube::Utils::CommandType::VALUE, ['Fetch'])
      ]
    )
  end

  def ok_response
    Hypertube::Utils::Command.new(Hypertube::Utils::RuntimeNameHt::JVM, Hypertube::Utils::CommandType::VALUE, ['ok'])
  end

  it 'records Graft dependency spans through the connector pipeline' do
    exporter = OpenTelemetry::SDK::Trace::Export::InMemorySpanExporter.new
    telemetry = Graftcode::Telemetry::Datadog.use(
      service_name: 'GraftSpanTest',
      console: false,
      span_exporter: exporter
    )

    context = Hypertube::Utils::Abstractions::HypertubeCallContext.new
    Hypertube::Utils::Abstractions::HypertubeBroker.on_execute(
      context,
      fetch_command,
      Hypertube::Utils::InMemoryConnectionData.new
    )
    Hypertube::Utils::Abstractions::HypertubeBroker.on_response(context, ok_response)

    finished = exporter.finished_spans
    caller = finished.find do |span|
      span.attributes[Graftcode::Instrumentation::CallSpan::DEPENDENCY_TYPE] == 'Graft'
    end

    expect(caller).not_to be_nil
    expect(caller.name).to eq('Graft SomeLib inmemory')
    expect(caller.attributes['hypertube.operation']).to eq('SomeLib.Service.Fetch')

    telemetry.shutdown
  end
end
