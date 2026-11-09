# frozen_string_literal: true

require 'opentelemetry-api'
require_relative 'hypertube_sdk'
require_relative 'body_capture'
require_relative 'library_catalog'
require_relative 'operation_name'
require_relative 'telemetry_settings'

module Graftcode
  module Instrumentation
    class CallSpan
      DEPENDENCY_TYPE = 'graft.dependency.type'
      DEPENDENCY_TARGET = 'graft.dependency.target'
      BRIDGE_DEPENDENCY_TYPE = 'Graft'
      REQUEST_BODY = 'hypertube.request.body'
      RESPONSE_BODY = 'hypertube.response.body'
      REQUEST_BODY_TRUNCATED = 'hypertube.request.body.truncated'
      RESPONSE_BODY_TRUNCATED = 'hypertube.response.body.truncated'
      TRACE_PARENT_HEADER = 'traceparent'
      TRACE_STATE_HEADER = 'tracestate'
      TRACE_ID_HEADER = 'traceid'

      SKIP_COMMAND_TYPES = [
        Hypertube::Utils::CommandType::PASS_BY_VALUE,
        Hypertube::Utils::CommandType::REGISTER_FOR_UPDATE,
        Hypertube::Utils::CommandType::VALUE_FOR_UPDATE,
        Hypertube::Utils::CommandType::INVOKE_DELEGATE
      ].freeze

      TRACE_CONTEXT = OpenTelemetry::Trace::Propagation::TraceContext.text_map_propagator

      module HeaderGetter
        module_function

        def get(carrier, key)
          CallSpan.header(carrier, key)
        end

        def keys(carrier)
          carrier.keys
        end
      end

      attr_reader :span

      def initialize(span, context_token)
        @span = span
        @context_token = context_token
        @injected_into = nil
        @previous_headers = nil
        @completed = false
      end

      def self.start_invoke(tracer, command, connection_data)
        operation = resolve_operation(command)
        return nil unless operation

        LibraryCatalog.record(command, connection_data)
        target = LibraryCatalog.describe_target(command, connection_data)
        span = start_span(tracer, "Graft #{target}", connection_data.get_headers)

        if span.recording?
          span.set_attribute('hypertube.runtime', LibraryCatalog.runtime_label(command.runtime_name))
          span.set_attribute('hypertube.command_type', Hypertube::Utils::CommandType.get_name(command.command_type))
          span.set_attribute('hypertube.connection_type', LibraryCatalog.connection_type_name(connection_data.connection_type))
          span.set_attribute(DEPENDENCY_TYPE, BRIDGE_DEPENDENCY_TYPE)
          set_if_present(span, DEPENDENCY_TARGET, Hypertube::Utils::Logging::LogsRedactor.redact_value(target))
          set_if_present(span, 'hypertube.operation', Hypertube::Utils::Logging::LogsRedactor.redact_value(operation))
          set_if_present(
            span,
            'hypertube.target',
            Hypertube::Utils::Logging::LogsRedactor.redact_value(
              Hypertube::Utils::Logging::LogsRedactor.redact_target(connection_data.hostname)
            )
          )
        end

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

      def complete(response, exception)
        return if @completed

        @completed = true
        begin
          capture(response, RESPONSE_BODY, RESPONSE_BODY_TRUNCATED) unless response.nil?

          if exception.nil?
            @span.status = OpenTelemetry::Trace::Status.ok
          else
            message = Hypertube::Utils::Logging::LogsRedactor.redact_value(exception.message)
            @span.set_attribute('exception.type', exception.class.name)
            set_if_present(@span, 'exception.message', message)
            @span.status = OpenTelemetry::Trace::Status.error(message.to_s)
          end

          if @injected_into
            @injected_into.set_thread_local_headers(@previous_headers || {})
          end
        ensure
          OpenTelemetry::Context.detach(@context_token) if @context_token
          @span.finish
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

      def self.complete_spans(context, response, exception)
        context.items.reverse_each do |item|
          next unless item.is_a?(CallSpan)

          item.complete(response, exception)
          context.items.delete(item)
        end
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

      def self.extract_parent_context(headers)
        carrier = headers || {}
        extracted = TRACE_CONTEXT.extract(carrier, context: OpenTelemetry::Context.empty, getter: HeaderGetter)
        return extracted if OpenTelemetry::Trace.current_span(extracted).context.valid?

        trace_id = read_trace_id(header(carrier, TRACE_ID_HEADER))
        current_trace_id = OpenTelemetry::Trace.current_span.context.hex_trace_id
        return OpenTelemetry::Context.current if trace_id.nil? || trace_id == current_trace_id

        remote_context = OpenTelemetry::Trace::SpanContext.new(
          trace_id: [trace_id].pack('H*'),
          span_id: OpenTelemetry::Trace.generate_span_id,
          trace_flags: OpenTelemetry::Trace::TraceFlags::SAMPLED,
          remote: true
        )
        OpenTelemetry::Trace.context_with_span(
          OpenTelemetry::Trace.non_recording_span(remote_context),
          parent_context: OpenTelemetry::Context.empty
        )
      end

      def inject_into(connection_data)
        local = connection_data.get_thread_local_headers
        span_context = @span.context
        return unless span_context.valid?

        merged = connection_data.get_headers
        if merged.key?(TRACE_PARENT_HEADER) && !local.key?(TRACE_PARENT_HEADER)
          return
        end

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

      def self.header(headers, key)
        value = headers[key]
        return value unless value.nil?

        headers.each do |entry_key, entry_value|
          return entry_value if entry_key.to_s.casecmp?(key)
        end
        nil
      end

      def self.read_trace_id(raw)
        return nil if raw.nil?

        hex = raw.strip.tr('-', '').downcase
        return nil unless hex.match?(/\A[0-9a-f]{32}\z/) && hex !~ /\A0+\z/

        hex
      end

      def self.set_if_present(span, key, value)
        span.set_attribute(key, value) unless value.nil?
      end

      def set_if_present(span, key, value)
        self.class.set_if_present(span, key, value)
      end
    end
  end
end
