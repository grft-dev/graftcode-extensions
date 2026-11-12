# frozen_string_literal: true

require 'opentelemetry-api'
require_relative 'hypertube_sdk'
require_relative 'body_capture'
require_relative 'telemetry_settings'
require_relative 'call_span/constants'
require_relative 'call_span/trace_propagation'
require_relative 'call_span/attributes'
require_relative 'call_span/factory'

module Graftcode
  module Instrumentation
    class CallSpan
      attr_reader :span

      def initialize(span, context_token)
        @span = span
        @context_token = context_token
        @injected_into = nil
        @previous_headers = nil
        @completed = false
      end

      def complete(response, exception)
        return if @completed

        @completed = true
        begin
          capture(response, RESPONSE_BODY, RESPONSE_BODY_TRUNCATED) unless response.nil?
          apply_completion_status(exception)
          restore_injected_headers
        ensure
          detach_and_finish
        end
      end

      def close
        complete(nil, nil)
      end

      def capture(command, body_key, truncated_key)
        return unless @span.recording? && TelemetrySettings.body_capture_enabled?

        rendered = BodyCapture.render(command, TelemetrySettings.body_max_characters)
        return if rendered.nil? || rendered.body.nil?

        @span.set_attribute(body_key, rendered.body)
        @span.set_attribute(truncated_key, true) if rendered.truncated
      end

      def inject_into(connection_data)
        local = connection_data.get_thread_local_headers
        span_context = @span.context
        return unless span_context.valid?

        merged = connection_data.get_headers
        return if merged.key?(TRACE_PARENT_HEADER) && !local.key?(TRACE_PARENT_HEADER)

        headers = local.dup
        headers.delete(TRACE_STATE_HEADER)
        TRACE_CONTEXT.inject(
          headers,
          context: OpenTelemetry::Trace.context_with_span(@span, parent_context: OpenTelemetry::Context.empty)
        )
        headers[TRACE_ID_HEADER] = span_context.hex_trace_id

        connection_data.set_thread_local_headers(headers)
        @injected_into = connection_data
        @previous_headers = local
      end

      private

      def apply_completion_status(exception)
        if exception.nil?
          @span.status = OpenTelemetry::Trace::Status.ok
          return
        end

        message = Hypertube::Utils::Logging::LogsRedactor.redact_value(exception.message)
        @span.set_attribute('exception.type', exception.class.name)
        self.class.set_if_present(@span, 'exception.message', message)
        @span.status = OpenTelemetry::Trace::Status.error(message.to_s)
      end

      def restore_injected_headers
        @injected_into&.set_thread_local_headers(@previous_headers || {})
      end

      def detach_and_finish
        OpenTelemetry::Context.detach(@context_token) if @context_token
        @span.finish
      end
    end
  end
end
