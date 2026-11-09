# frozen_string_literal: true

require 'tmpdir'
require_relative 'support/harness'

RSpec.describe Graftcode::Instrumentation::CallSpan do
  include Graftcode::Instrumentation::Support::Harness::Commands

  let(:trace_id) { '0af7651916cd43dd8448eb211c80319c' }
  let(:parent_span_id) { 'b7ad6b7169203331' }
  let(:trace_parent) { "00-#{trace_id}-#{parent_span_id}-01" }
  let(:harness_class) { Graftcode::Instrumentation::Support::Harness }
  let(:command_type) { Hypertube::Utils::CommandType }
  let(:status) { OpenTelemetry::Trace::Status }

  def in_memory
    Hypertube::Utils::InMemoryConnectionData.new
  end

  def single(harness)
    spans = harness.finished_spans
    expect(spans.length).to eq(1)
    spans.first
  end

  def trace_parent_of(call_span)
    context = call_span.span.context
    "00-#{context.hex_trace_id}-#{context.hex_span_id}-#{context.trace_flags.sampled? ? '01' : '00'}"
  end

  it 'opens a dependency span named after the target on invoke' do
    harness_class.open do |harness|
      harness.complete(harness.on_invoke(fetch, in_memory), value('ok'), nil)

      stopped = single(harness)
      expect(stopped.instrumentation_scope.name).to eq(Graftcode::Instrumentation::GraftcodeInstrumentation::INSTRUMENTATION_NAME)
      expect(stopped.name).to eq('Graft SomeLib inmemory')
      expect(stopped.attributes).to include(
        described_class::DEPENDENCY_TYPE => 'Graft',
        described_class::DEPENDENCY_TARGET => 'SomeLib inmemory',
        'hypertube.operation' => 'SomeLib.Service.Fetch',
        'hypertube.command_type' => 'INVOKE_STATIC_METHOD',
        'hypertube.runtime' => 'Jvm',
        'hypertube.connection_type' => 'IN_MEMORY',
        'hypertube.target' => 'inmemory'
      )
      expect(stopped.status.code).to eq(status::OK)
      expect(OpenTelemetry::Trace.current_span.context).not_to be_valid
    end
  end

  it 'writes the span into the headers and restores them' do
    harness_class.open do |harness|
      connection = in_memory
      connection.set_thread_local_headers('tenant' => 't1')

      call_span = harness.on_invoke(fetch, connection)

      expect(connection.get_headers[described_class::TRACE_PARENT_HEADER]).to eq(trace_parent_of(call_span))
      expect(connection.get_headers[described_class::TRACE_ID_HEADER]).to eq(call_span.span.context.hex_trace_id)
      expect(connection.get_headers['tenant']).to eq('t1')

      harness.complete(call_span, nil, nil)

      expect(connection.get_headers).not_to include(described_class::TRACE_PARENT_HEADER)
      expect(connection.get_headers).not_to include(described_class::TRACE_ID_HEADER)
      expect(connection.get_headers['tenant']).to eq('t1')
    end
  end

  it 'injects only the W3C trace context, even when baggage is propagated globally' do
    previous = OpenTelemetry.propagation
    OpenTelemetry.propagation = OpenTelemetry::Context::Propagation::CompositeTextMapPropagator.compose_propagators(
      [OpenTelemetry::Trace::Propagation::TraceContext.text_map_propagator, OpenTelemetry::Baggage::Propagation.text_map_propagator]
    )

    harness_class.open do |harness|
      connection = in_memory
      OpenTelemetry::Context.with_current(OpenTelemetry::Baggage.set_value('user', 'ada')) do
        call_span = harness.on_invoke(fetch, connection)
        expect(connection.get_headers.keys).to contain_exactly(described_class::TRACE_PARENT_HEADER, described_class::TRACE_ID_HEADER)
        harness.complete(call_span, nil, nil)
      end
    end
  ensure
    OpenTelemetry.propagation = previous
  end

  it 'does not replace a trace the caller already set' do
    harness_class.open do |harness|
      connection = in_memory
      connection.set_headers(described_class::TRACE_PARENT_HEADER => trace_parent)

      call_span = harness.on_invoke(fetch, connection)

      expect(call_span.span.context.hex_trace_id).to eq(trace_id)
      expect(connection.get_headers[described_class::TRACE_PARENT_HEADER]).to eq(trace_parent)
      harness.complete(call_span, nil, nil)
      expect(single(harness).hex_parent_span_id).to eq(parent_span_id)
    ensure
      connection&.set_headers({})
    end
  end

  it 'propagates the inner span of a nested invoke and restores the outer one' do
    harness_class.open do |harness|
      connection = in_memory

      outer = harness.on_invoke(fetch, connection)
      inner = harness.on_invoke(fetch, connection)

      expect(connection.get_headers[described_class::TRACE_PARENT_HEADER]).to eq(trace_parent_of(inner))

      harness.complete(inner, nil, nil)
      expect(connection.get_headers[described_class::TRACE_PARENT_HEADER]).to eq(trace_parent_of(outer))
      expect(OpenTelemetry::Trace.current_span.context.hex_span_id).to eq(outer.span.context.hex_span_id)

      harness.complete(outer, nil, nil)
      expect(connection.get_headers).not_to include(described_class::TRACE_PARENT_HEADER)
      expect(OpenTelemetry::Trace.current_span.context).not_to be_valid
      expect(harness.finished_spans.first.hex_parent_span_id).to eq(outer.span.context.hex_span_id)
    end
  end

  it 'leaves the headers alone when auto-inject is disabled' do
    harness_class.open do |harness|
      Graftcode::Instrumentation::TelemetrySettings.auto_inject_enabled = false
      connection = in_memory

      call_span = harness.on_invoke(fetch, connection)

      expect(connection.get_headers).not_to include(described_class::TRACE_PARENT_HEADER)
      harness.complete(call_span, nil, nil)
    end
  end

  it 'stops a failed call with a redacted message' do
    harness_class.open do |harness|
      harness.complete(harness.on_invoke(fetch, in_memory), nil, RuntimeError.new('Password=hunter2'))

      stopped = single(harness)
      expect(stopped.status.code).to eq(status::ERROR)
      expect(stopped.status.description).to eq(Hypertube::Utils::Logging::LogsRedactor::PLACEHOLDER)
      expect(stopped.attributes['exception.type']).to eq('RuntimeError')
      expect(stopped.attributes['exception.message']).to eq(Hypertube::Utils::Logging::LogsRedactor::PLACEHOLDER)
      expect(stopped.events.to_a).to be_empty
    end
  end

  it 'reports an exception response as a completed call, like Java and .NET' do
    harness_class.open do |harness|
      response = of(command_type::EXCEPTION, 0, 'trace', 'ArgumentError', 'bad input')
      harness.complete(harness.on_handle(fetch), response, nil)

      stopped = single(harness)
      expect(stopped.status.code).to eq(status::OK)
      expect(stopped.attributes).not_to include('exception.type')
    end
  end

  %i[INVOKE_DELEGATE PASS_BY_VALUE REGISTER_FOR_UPDATE VALUE_FOR_UPDATE].each do |type|
    it "opens no span for the #{type} transport command" do
      harness_class.open do |harness|
        expect(harness.on_invoke(of(command_type.const_get(type), 'x'), in_memory)).to be_nil
        expect(harness.on_handle(of(command_type.const_get(type), 'x'))).to be_nil
        expect(harness.finished_spans).to be_empty
      end
    end
  end

  it 'opens no span for telemetry setup calls' do
    harness_class.open do |harness|
      setup = of(
        command_type::INVOKE_STATIC_METHOD,
        of(command_type::GET_TYPE, value('app.TelemetrySetup')),
        value('initialize')
      )
      expect(harness.on_handle(setup)).to be_nil
    end
  end

  it 'gives an unrecorded span no attributes but still propagates it' do
    harness_class.open(recording: false) do |harness|
      connection = in_memory

      call_span = harness.on_invoke(fetch, connection)

      expect(call_span.span).not_to be_recording
      expect(connection.get_headers[described_class::TRACE_PARENT_HEADER]).to eq(trace_parent_of(call_span))
      harness.complete(call_span, nil, nil)
      expect(harness.finished_spans).to be_empty
    end
  end

  it 'names the handle span after the operation and parents it on the incoming traceparent' do
    harness_class.open do |harness|
      harness.complete(harness.on_handle(fetch, described_class::TRACE_PARENT_HEADER => trace_parent), nil, nil)

      stopped = single(harness)
      expect(stopped.name).to eq('SomeLib.Service.Fetch')
      expect(stopped.hex_trace_id).to eq(trace_id)
      expect(stopped.hex_parent_span_id).to eq(parent_span_id)
      expect(stopped.attributes).not_to include(described_class::DEPENDENCY_TYPE)
    end
  end

  it 'reads the incoming traceparent regardless of header case' do
    harness_class.open do |harness|
      harness.complete(harness.on_handle(fetch, 'TraceParent' => trace_parent), nil, nil)

      expect(single(harness).hex_parent_span_id).to eq(parent_span_id)
    end
  end

  it 'falls back to the bare traceid on the handle side' do
    harness_class.open do |harness|
      harness.complete(harness.on_handle(fetch, described_class::TRACE_ID_HEADER => trace_id), nil, nil)

      stopped = single(harness)
      expect(stopped.hex_trace_id).to eq(trace_id)
      expect(stopped.hex_parent_span_id).not_to eq('0000000000000000')
    end
  end

  it 'names the target of later calls after a loaded library and its gemspec version' do
    Dir.mktmpdir do |directory|
      library = File.join(directory, 'TestGgRubyLib.rb')
      File.write(library, '')
      File.write(File.join(directory, 'TestGgRubyLib.gemspec'), <<~GEMSPEC)
        Gem::Specification.new do |spec|
          spec.name = 'TestGgRubyLib'
          spec.version = '1.2.3'
          spec.summary = 'test'
          spec.authors = ['test']
        end
      GEMSPEC

      harness_class.open do |harness|
        connection = in_memory
        harness.complete(harness.on_invoke(of(command_type::LOAD_LIBRARY, value(library)), connection), nil, nil)
        fetch_random = of(
          command_type::INVOKE_STATIC_METHOD,
          of(command_type::GET_TYPE, value('TestGgRubyLib::RandomService')),
          value('fetch')
        )
        harness.complete(harness.on_invoke(fetch_random, connection), nil, nil)

        expect(harness.finished_spans[1].name).to eq('Graft TestGgRubyLib 1.2.3 inmemory')
      end
    end
  end

  it 'names the target after the root of a Ruby namespace when no library was loaded' do
    harness_class.open do |harness|
      fetch_random = of(
        command_type::INVOKE_STATIC_METHOD,
        of(command_type::GET_TYPE, value('TestGgRubyLib::RandomService')),
        value('fetch')
      )
      harness.complete(harness.on_invoke(fetch_random, in_memory), nil, nil)

      expect(single(harness).name).to eq('Graft TestGgRubyLib inmemory')
    end
  end
end
