# frozen_string_literal: true

require 'stringio'
require 'graftcode/telemetry/datadog'
require 'opentelemetry/sdk/logs/export/in_memory_log_record_exporter'

module InMemoryTelemetry
  def start_telemetry(service_name = 'TestService', **settings)
    @spans = OpenTelemetry::SDK::Trace::Export::InMemorySpanExporter.new
    @logs = OpenTelemetry::SDK::Logs::Export::InMemoryLogRecordExporter.new
    options = Graftcode::Telemetry::Datadog::GraftCodeDatadogConnectorOptions.new({})
    options.enable_console_logging = false
    settings.each { |key, value| options.public_send("#{key}=", value) }

    Graftcode::Telemetry::Datadog::GraftCodeDatadogConnector.start(
      service_name,
      options: options,
      span_exporter: @spans,
      log_record_exporter: @logs
    )
  end

  def finished_spans
    @spans.finished_spans
  end

  def emitted_logs
    @logs.emitted_log_records
  end
end

RSpec.configure do |config|
  config.include InMemoryTelemetry

  config.after do
    Graftcode::Telemetry::Datadog::GraftCodeDatadogConnector.active&.close
  end
end
