# frozen_string_literal: true

require 'graftcode/telemetry/datadog'

RSpec.describe Graftcode::Telemetry::Datadog do
  it 'starts locally without export credentials and warns once' do
    telemetry = described_class.use(service_name: 'LocalService', console: false)
    expect(telemetry.exporting).to be(false)
    expect(described_class.active).to equal(telemetry)
    telemetry.shutdown
  end

  it 'returns the same instance for the same service name' do
    first = described_class.use(service_name: 'Same', console: false)
    second = described_class.use(service_name: 'Same', console: false)
    expect(second).to equal(first)
    first.shutdown
  end

  it 'attaches graftcode instrumentation to Hypertube' do
    telemetry = described_class.use(
      service_name: 'HypertubeAttach',
      console: false,
      span_exporter: OpenTelemetry::SDK::Trace::Export::InMemorySpanExporter.new
    )
    expect(Hypertube::Utils::Logging::HypertubeLogger.enabled?(Hypertube::Utils::Logging::LogsLevel::INFO)).to be(true)
    telemetry.shutdown
    expect(Hypertube::Utils::Logging::HypertubeLogger.enabled?(Hypertube::Utils::Logging::LogsLevel::INFO)).to be(false)
  end
end
