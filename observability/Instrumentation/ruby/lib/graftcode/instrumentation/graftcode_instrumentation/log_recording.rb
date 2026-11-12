# frozen_string_literal: true

module Graftcode
  module Instrumentation
    class GraftcodeInstrumentation
      module LogRecording
        SEVERITY_NUMBER = {
          Hypertube::Utils::Logging::LogsLevel::TRACE => 1,
          Hypertube::Utils::Logging::LogsLevel::DEBUG => 5,
          Hypertube::Utils::Logging::LogsLevel::INFO => 9,
          Hypertube::Utils::Logging::LogsLevel::WARNING => 13,
          Hypertube::Utils::Logging::LogsLevel::ERROR => 17
        }.freeze

        private

        def emit_log_record(level, message, properties, exception)
          @logger.on_emit(
            timestamp: Time.now,
            severity_number: SEVERITY_NUMBER.fetch(level, 0),
            severity_text: severity_text(level),
            body: message,
            attributes: log_record_attributes(properties, exception),
            context: OpenTelemetry::Context.current
          )
        end

        def record_span_event(level, message, properties, exception)
          span = current_span
          return unless span.recording?

          if exception.nil?
            span.add_event(message, attributes: base_event_attributes(level, properties))
            return
          end

          span.add_event('exception', attributes: exception_event_attributes(level, message, properties, exception))
        end

        def log_record_attributes(properties, exception)
          attributes = properties.to_h.dup
          merge_exception_attributes!(attributes, exception)
          attributes
        end

        def base_event_attributes(level, properties)
          attributes = { 'log.level' => severity_text(level) }
          properties.each { |key, value| attributes[key] = value }
          attributes
        end

        def exception_event_attributes(level, message, properties, exception)
          attributes = base_event_attributes(level, properties)
          attributes['log.message'] = message
          merge_exception_attributes!(attributes, exception)
          attributes
        end

        def merge_exception_attributes!(attributes, exception)
          return if exception.nil?

          attributes['exception.type'] = exception.class.name
          set_if_present(attributes, 'exception.message', Hypertube::Utils::Logging::LogsRedactor.redact_value(exception.message))
          set_if_present(attributes, 'exception.stacktrace', Hypertube::Utils::Logging::LogsRedactor.redact_value(stack_trace(exception)))
        end

        def severity_text(level)
          level == Hypertube::Utils::Logging::LogsLevel::INFO ? 'Information' : Hypertube::Utils::Logging::LogsLevel.name_for(level)
        end

        def stack_trace(exception)
          exception.full_message(highlight: false, order: :top)
        end

        def set_if_present(attributes, key, value)
          attributes[key] = value unless value.nil?
        end
      end
    end
  end
end
