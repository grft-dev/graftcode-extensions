# frozen_string_literal: true

RSpec.describe Graftcode::Telemetry::Datadog::GraftCodeDatadogConnector do
  it 'returns the running pipeline when started again for the same service' do
    telemetry = start_telemetry('Orders')

    expect(described_class.start('Orders')).to be(telemetry)
  end

  it 'refuses to start a second service in the same process' do
    start_telemetry('Orders')

    expect { described_class.start('Billing') }.to raise_error(ArgumentError, /already started for Orders/)
  end

  it 'requires a service name' do
    options = Graftcode::Telemetry::Datadog::GraftCodeDatadogConnectorOptions.new({})

    expect { described_class.start(options: options) }.to raise_error(ArgumentError, /service_name/)
  end

  it 'can be started again after close' do
    start_telemetry('Orders').close

    expect(described_class.active).to be_nil
    expect(start_telemetry('Billing').service_name).to eq('Billing')
  end

  it 'describes the service, environment and version on the resource' do
    telemetry = start_telemetry('Orders', environment: 'local', version: '1.2.3')
    telemetry.in_span('work') { |span| span }

    attributes = finished_spans.first.resource.attribute_enumerator.to_h
    expect(attributes['service.name']).to eq('Orders')
    expect(attributes['deployment.environment.name']).to eq('local')
    expect(attributes['service.version']).to eq('1.2.3')
  end

  it 'attaches Graftcode instrumentation to the Hypertube broker until closed' do
    telemetry = start_telemetry
    listeners = Hypertube::Utils::Abstractions::HypertubeBroker.listeners

    expect(listeners).to include(an_instance_of(Graftcode::Instrumentation::GraftcodeInstrumentation))

    telemetry.close
    expect(Hypertube::Utils::Abstractions::HypertubeBroker.listeners)
      .not_to include(an_instance_of(Graftcode::Instrumentation::GraftcodeInstrumentation))
  end

  it 'flushes after each handled call when hosted, after the instrumentation has completed its span' do
    telemetry = start_telemetry(flush_after_calls: true)
    listeners = Hypertube::Utils::Abstractions::HypertubeBroker.listeners
    flusher = listeners.find { |listener| listener.is_a?(Graftcode::Telemetry::Datadog::CallFlusher) }

    expect(telemetry.flushes_after_calls?).to be(true)
    expect(listeners.index(flusher)).to be > listeners.index { |l| l.is_a?(Graftcode::Instrumentation::GraftcodeInstrumentation) }

    allow(telemetry).to receive(:flush).and_call_original
    context = Hypertube::Utils::Abstractions::HypertubeCallContext.new
    Hypertube::Utils::Abstractions::HypertubeBroker.on_processed(context, nil)
    Hypertube::Utils::Abstractions::HypertubeBroker.on_handling_error(context, RuntimeError.new('boom'))

    expect(telemetry).to have_received(:flush).twice
    telemetry.close
    expect(Hypertube::Utils::Abstractions::HypertubeBroker.listeners).not_to include(flusher)
  end

  it 'exports Hypertube logs as log records correlated with the current span' do
    telemetry = start_telemetry

    telemetry.in_span('call') do
      Hypertube::Utils::Logging::HypertubeLogger.get_logger.log_with_level(
        Hypertube::Utils::Logging::LogsLevel::INFO,
        'Hypertube invoking {CommandType} on the {RuntimeName} runtime',
        'INVOKE_STATIC_METHOD',
        'Ruby'
      )
    end

    record = emitted_logs.find { |log| log.instrumentation_scope.name == 'Hypertube' }
    expect(record.body).to eq('Hypertube invoking INVOKE_STATIC_METHOD on the Ruby runtime')
    expect(record.attributes).to include('CommandType' => 'INVOKE_STATIC_METHOD', 'RuntimeName' => 'Ruby')
    expect(record.trace_id).to eq(finished_spans.first.trace_id)
    expect(record.span_id).to eq(finished_spans.first.span_id)
  end

  it 'leaves exporting to the batch threads in a regular Ruby process' do
    expect(start_telemetry.flushes_after_calls?).to be(false)
  end

  it 'propagates W3C trace context' do
    telemetry = start_telemetry
    carrier = {}

    telemetry.in_span('outgoing') { OpenTelemetry.propagation.inject(carrier) }

    expect(carrier['traceparent']).to start_with("00-#{finished_spans.first.hex_trace_id}-")
  end
end
