# frozen_string_literal: true

RSpec.describe Graftcode::Telemetry::Datadog::GraftCodeDatadogConnectorOptions do
  it 'reads the OTLP endpoint, environment and version from the environment' do
    options = described_class.new(
      'OTEL_EXPORTER_OTLP_ENDPOINT' => 'http://agent:4318/',
      'DD_ENV' => 'staging',
      'DD_VERSION' => '2.0'
    )

    expect(options.resolve_endpoint).to eq('http://agent:4318')
    expect(options.resolve_environment).to eq('staging')
    expect(options.resolve_version).to eq('2.0')
  end

  it 'defaults to the local Datadog Agent OTLP/HTTP receiver' do
    expect(described_class.new({}).resolve_endpoint).to eq('http://127.0.0.1:4318')
  end

  it 'flushes after calls only inside the Hypertube native launcher unless configured' do
    options = described_class.new({})
    hosted = ['C:/app/.graftcode/1.4.1/Binaries/Ruby/core/receiver/receiver.rb']
    gem = ['C:/Ruby/gems/hypertube-ruby-sdk-3.1.18/lib/hypertube-ruby-sdk/core/receiver/receiver.rb']

    expect(options.resolve_flush_after_calls(hosted)).to be(true)
    expect(options.resolve_flush_after_calls(gem)).to be(false)

    options.flush_after_calls = false
    expect(options.resolve_flush_after_calls(hosted)).to be(false)
  end

  it 'accepts the minimum log level by name' do
    options = described_class.new({})
    options.minimum_log_level = 'warning'

    expect(options.minimum_log_level).to eq(Hypertube::Utils::Logging::LogsLevel::WARNING)
  end
end
