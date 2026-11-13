# frozen_string_literal: true

require 'opentelemetry-api'

module Graftcode
  module Instrumentation
    class CallSpan
      module HeaderGetter
        module_function

        def get(carrier, key)
          CallSpan.header(carrier, key)
        end

        def keys(carrier)
          carrier.keys
        end
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

      def self.extract_parent_context(headers)
        carrier = headers || {}
        extracted = TRACE_CONTEXT.extract(carrier, context: OpenTelemetry::Context.empty, getter: HeaderGetter)
        return extracted if trace_context_valid?(extracted)

        parent_context_from_trace_id_header(carrier)
      end

      def self.trace_context_valid?(context)
        OpenTelemetry::Trace.current_span(context).context.valid?
      end

      def self.parent_context_from_trace_id_header(carrier)
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
    end
  end
end
