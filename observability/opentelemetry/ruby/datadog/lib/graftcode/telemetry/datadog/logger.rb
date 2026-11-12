# frozen_string_literal: true

require 'opentelemetry-logs-sdk'
require 'graftcode/instrumentation'
require_relative 'message_template'

module Graftcode
  module Telemetry
    module Datadog
      class GraftCodeDatadogLogger
        LogsLevel = Hypertube::Utils::Logging::LogsLevel
        SeverityNumber = OpenTelemetry::Logs::SeverityNumber

        CONSOLE_INDENT = '      '
        ORIGINAL_FORMAT = '{OriginalFormat}'

        SEVERITY = {
          LogsLevel::TRACE => [SeverityNumber::SEVERITY_NUMBER_TRACE, 'trace', 'trce'],
          LogsLevel::DEBUG => [SeverityNumber::SEVERITY_NUMBER_DEBUG, 'debug', 'dbug'],
          LogsLevel::INFO => [SeverityNumber::SEVERITY_NUMBER_INFO, 'info', 'info'],
          LogsLevel::WARNING => [SeverityNumber::SEVERITY_NUMBER_WARN, 'warn', 'warn'],
          LogsLevel::ERROR => [SeverityNumber::SEVERITY_NUMBER_ERROR, 'error', 'fail']
        }.freeze

        attr_reader :category, :minimum_level

        def initialize(category, otel_logger, minimum_level, console)
          @category = category
          @otel_logger = otel_logger
          @minimum_level = minimum_level
          @console = console
          @console_mutex = Mutex.new
        end

        def enabled?(level)
          level != LogsLevel::OFF && minimum_level != LogsLevel::OFF && LogsLevel.at_least?(level, minimum_level)
        end

        def trace(template, *args, exception: nil)
          log(LogsLevel::TRACE, template, *args, exception: exception)
        end

        def debug(template, *args, exception: nil)
          log(LogsLevel::DEBUG, template, *args, exception: exception)
        end

        def info(template, *args, exception: nil)
          log(LogsLevel::INFO, template, *args, exception: exception)
        end

        def warning(template, *args, exception: nil)
          log(LogsLevel::WARNING, template, *args, exception: exception)
        end
        alias warn warning

        def error(template, *args, exception: nil)
          log(LogsLevel::ERROR, template, *args, exception: exception)
        end

        def log(level, template, *args, exception: nil)
          return unless enabled?(level)

          properties = {}
          message = MessageTemplate.render(template, args, properties)

          emit(level, template, message, properties, exception)
          write_to_console(level, message, exception)
        end

        private

        def emit(level, template, message, properties, exception)
          severity_number, severity_text, = SEVERITY.fetch(level)
          @otel_logger.on_emit(
            timestamp: Time.now,
            severity_number: severity_number,
            severity_text: severity_text,
            body: message,
            attributes: emit_attributes(template, properties, exception),
            context: OpenTelemetry::Context.current
          )
        end

        def emit_attributes(template, properties, exception)
          attributes = properties.dup
          attributes[ORIGINAL_FORMAT] = template unless template.nil?
          merge_exception_attributes!(attributes, exception)
          attributes
        end

        def merge_exception_attributes!(attributes, exception)
          return if exception.nil?

          attributes['exception.type'] = exception.class.name
          attributes['exception.message'] = exception.message unless exception.message.nil?
          attributes['exception.stacktrace'] = exception.full_message(highlight: false)
        end

        def write_to_console(level, message, exception)
          return if @console.nil?

          line = "#{SEVERITY.fetch(level)[2]}: #{category}[0]\n#{CONSOLE_INDENT}#{message}"
          line << "\n#{exception.full_message(highlight: false).strip}" unless exception.nil?

          @console_mutex.synchronize { @console.puts(line) }
        end
      end
    end
  end
end
