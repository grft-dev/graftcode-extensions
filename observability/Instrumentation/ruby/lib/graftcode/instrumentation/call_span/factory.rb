# frozen_string_literal: true

require_relative '../hypertube_sdk'
require_relative '../library_catalog'
require_relative '../operation_name'
require_relative '../telemetry_settings'

module Graftcode
  module Instrumentation
    class CallSpan
      def self.start_invoke(tracer, command, connection_data)
        operation = resolve_operation(command)
        return nil unless operation

        LibraryCatalog.record(command, connection_data)
        target = LibraryCatalog.describe_target(command, connection_data)
        span = start_span(tracer, "Graft #{target}", connection_data.get_headers)
        apply_invoke_attributes(span, command, connection_data, target, operation) if span.recording?

        call_span = attach(span)
        call_span.capture(command, REQUEST_BODY, REQUEST_BODY_TRUNCATED)
        call_span.inject_into(connection_data) if TelemetrySettings.auto_inject_enabled?
        call_span
      end

      def self.start_handle(tracer, command, headers)
        operation = resolve_operation(command)
        return nil unless operation

        call_span = attach(start_span(tracer, operation, headers || {}))
        call_span.capture(command, REQUEST_BODY, REQUEST_BODY_TRUNCATED)
        call_span
      end

      def self.resolve_operation(command)
        return nil unless command.is_a?(Hypertube::Utils::Command)
        return nil if SKIP_COMMAND_TYPES.include?(command.command_type)

        operation = OperationName.resolve(command)
        return nil if operation.downcase.end_with?('telemetrysetup.initialize')

        operation
      end

      def self.attach(span)
        token = OpenTelemetry::Context.attach(OpenTelemetry::Trace.context_with_span(span))
        new(span, token)
      end

      def self.start_span(tracer, name, headers)
        parent = extract_parent_context(headers)
        tracer.start_span(name, with_parent: parent, kind: :internal)
      end

      def self.complete_spans(context, response, exception)
        context.items.reverse_each do |item|
          next unless item.is_a?(CallSpan)

          item.complete(response, exception)
          context.items.delete(item)
        end
      end
    end
  end
end
