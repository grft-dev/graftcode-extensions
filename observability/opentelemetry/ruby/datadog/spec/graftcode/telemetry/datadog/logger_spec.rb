# frozen_string_literal: true

RSpec.describe Graftcode::Telemetry::Datadog::GraftCodeDatadogLogger do
  it 'emits a log record correlated with the current span' do
    telemetry = start_telemetry

    telemetry.in_span('work') do
      telemetry.logger.info('Fetched {Input} in {Elapsed}ms', 'abc', 12)
    end

    record = emitted_logs.first
    span = finished_spans.first
    expect(record.body).to eq('Fetched abc in 12ms')
    expect(record.severity_text).to eq('info')
    expect(record.severity_number).to eq(OpenTelemetry::Logs::SeverityNumber::SEVERITY_NUMBER_INFO)
    expect(record.attributes).to include('Input' => 'abc', 'Elapsed' => '12', '{OriginalFormat}' => 'Fetched {Input} in {Elapsed}ms')
    expect(record.trace_id.unpack1('H*')).to eq(span.hex_trace_id)
    expect(record.span_id.unpack1('H*')).to eq(span.hex_span_id)
  end

  it 'drops records below the minimum level' do
    telemetry = start_telemetry(minimum_log_level: Hypertube::Utils::Logging::LogsLevel::WARNING)

    telemetry.logger.info('ignored')
    telemetry.logger.warning('kept')

    expect(emitted_logs.map(&:body)).to eq(['kept'])
  end

  it 'records exception details' do
    telemetry = start_telemetry
    error = RuntimeError.new('boom')

    telemetry.logger.error('Request {Path} failed', '/api', exception: error)

    expect(emitted_logs.first.attributes).to include('exception.type' => 'RuntimeError', 'exception.message' => 'boom')
    expect(emitted_logs.first.severity_text).to eq('error')
  end

  it 'writes to the console in the .NET console format' do
    console = StringIO.new
    otel_logger = OpenTelemetry::SDK::Logs::LoggerProvider.new.logger(name: 'test')
    logger = described_class.new('Orders', otel_logger, Hypertube::Utils::Logging::LogsLevel::INFO, console)

    logger.warning('Low stock for {Sku}', 'A1')

    expect(console.string).to eq("warn: Orders[0]\n      Low stock for A1\n")
  end
end

RSpec.describe Graftcode::Telemetry::Datadog::MessageTemplate do
  it 'renders placeholders and captures them as properties' do
    properties = {}

    message = described_class.render('{@User} bought {Count,3:D} {{items}}', ['ann', nil], properties)

    expect(message).to eq('ann bought (null) {items}')
    expect(properties).to eq('User' => 'ann', 'Count' => '(null)')
  end

  it 'leaves placeholders without arguments untouched' do
    expect(described_class.render('Hello {Name}', [], {})).to eq('Hello {Name}')
  end
end
