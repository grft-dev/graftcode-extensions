# frozen_string_literal: true

require_relative 'support/harness'

RSpec.describe Graftcode::Instrumentation::CallSpan do
  include Graftcode::Instrumentation::Support::Harness::Commands

  def single(harness)
    spans = harness.finished_spans
    expect(spans.length).to eq(1)
    spans.first
  end

  def attribute(span, key)
    span.attributes[key]
  end

  it 'opens a dependency span named after the target on invoke' do
    harness = Graftcode::Instrumentation::Support::Harness.new
    begin
      harness.complete(harness.on_invoke(fetch, Hypertube::Utils::InMemoryConnectionData.new), value('ok'), nil)

      stopped = single(harness)
      expect(stopped.name).to eq('Graft SomeLib inmemory')
      expect(attribute(stopped, described_class::DEPENDENCY_TYPE)).to eq('Graft')
      expect(attribute(stopped, described_class::DEPENDENCY_TARGET)).to eq('SomeLib inmemory')
      expect(attribute(stopped, 'hypertube.operation')).to eq('SomeLib.Service.Fetch')
      expect(attribute(stopped, 'hypertube.command_type')).to eq('INVOKE_STATIC_METHOD')
      expect(attribute(stopped, 'hypertube.runtime')).to eq('Jvm')
      expect(attribute(stopped, 'hypertube.connection_type')).to eq('IN_MEMORY')
      expect(attribute(stopped, 'hypertube.target')).to eq('inmemory')
      expect(stopped.status.code).to eq(0)
    ensure
      harness.close
    end
  end

  it 'writes trace context into headers and restores them on complete' do
    harness = Graftcode::Instrumentation::Support::Harness.new
    begin
      connection = Hypertube::Utils::InMemoryConnectionData.new
      connection.set_thread_local_headers('tenant' => 't1')

      span = harness.on_invoke(fetch, connection)
      expect(connection.get_headers[described_class::TRACE_PARENT_HEADER]).not_to be_nil
      expect(connection.get_headers[described_class::TRACE_ID_HEADER]).to eq(span.span.context.hex_trace_id)
      expect(connection.get_headers['tenant']).to eq('t1')

      harness.complete(span, nil, nil)

      expect(connection.get_headers).not_to include(described_class::TRACE_PARENT_HEADER)
      expect(connection.get_headers).not_to include(described_class::TRACE_ID_HEADER)
      expect(connection.get_headers['tenant']).to eq('t1')
    ensure
      harness.close
    end
  end

  it 'names the handle-side span after the operation' do
    harness = Graftcode::Instrumentation::Support::Harness.new
    begin
      span = harness.on_handle(fetch)
      harness.complete(span, value('ok'), nil)

      stopped = single(harness)
      expect(stopped.name).to eq('SomeLib.Service.Fetch')
      expect(attribute(stopped, described_class::DEPENDENCY_TYPE)).to be_nil
    ensure
      harness.close
    end
  end
end
