# frozen_string_literal: true

require 'opentelemetry-api'

module Graftcode
  module Telemetry
    module Datadog
      class AppLogger
        SEVERITIES = {
          Hypertube::Utils::Logging::LogsLevel::DEBUG => [5, 'DEBUG', 'dbug'],
          Hypertube::Utils::Logging::LogsLevel::INFO => [9, 'INFO', 'info'],
          Hypertube::Utils::Logging::LogsLevel::WARNING => [13, 'WARN', 'warn'],
          Hypertube::Utils::Logging::LogsLevel::ERROR => [17, 'ERROR', 'fail']
        }.freeze

        def initialize(service_name, minimum_level, console: true)
          @service_name = service_name
          @minimum_level = minimum_level
          @console = console
          @otel_logger = OpenTelemetry.logger_provider.logger(service_name) if logger_provider?
        end

        def debug(message, attributes = {})
          write(Hypertube::Utils::Logging::LogsLevel::DEBUG, message, attributes)
        end

        def info(message, attributes = {})
          write(Hypertube::Utils::Logging::LogsLevel::INFO, message, attributes)
        end

        def warn(message, attributes = {})
          write(Hypertube::Utils::Logging::LogsLevel::WARNING, message, attributes)
        end

        def error(message, error_or_attributes = {})
          write(Hypertube::Utils::Logging::LogsLevel::ERROR, message, error_or_attributes)
        end

        private

        def write(level, message, details)
          return unless Hypertube::Utils::Logging::LogsLevel.at_least?(level, @minimum_level)

          severity_number, severity_text, label = SEVERITIES.fetch(level)
          attributes = details.is_a?(Exception) ? exception_attributes(details) : details.dup
          emit_otel_log(severity_number, severity_text, message, attributes)
          write_console(label, message, details) if @console
        end

        def emit_otel_log(severity_number, severity_text, message, attributes)
          return unless @otel_logger

          @otel_logger.on_emit(
            timestamp: Time.now,
            severity_number: severity_number,
            severity_text: severity_text,
            body: message,
            attributes: attributes,
            context: OpenTelemetry::Context.current
          )
        end

        def write_console(label, message, details)
          suffix = details.is_a?(Exception) ? " #{details.class}: #{details.message}" : ''
          $stdout.write("#{label}: #{@service_name}: #{message}#{suffix}\n")
        end

        def exception_attributes(error)
          {
            'exception.type' => error.class.name,
            'exception.message' => Hypertube::Utils::Logging::LogsRedactor.redact_value(error.message).to_s,
            'exception.stacktrace' => error.full_message
          }
        end

        def logger_provider?
          OpenTelemetry.logger_provider &&
            !OpenTelemetry.logger_provider.is_a?(OpenTelemetry::Logs::Provider::NoopLogsProvider)
        rescue NameError
          false
        end
      end
    end
  end
end
