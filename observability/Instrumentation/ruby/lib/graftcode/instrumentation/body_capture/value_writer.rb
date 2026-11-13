# frozen_string_literal: true

require_relative '../hypertube_sdk'

module Graftcode
  module Instrumentation
    module BodyCapture
      module_function

      def write_value(ctx, value, force_redact: false)
        return write_null(ctx) if value.nil?
        return write_at_depth_limit(ctx) if ctx.depth >= MAX_DEPTH
        if value.is_a?(Hypertube::Utils::Command)
          return write_command(ctx.writer, value, ctx.depth, ctx.budget, ctx.mark_truncated)
        end

        return write_binary_bytes(ctx, value) if binary_string?(value)
        return write_redacted_string(ctx, value, force_redact) if value.is_a?(String)

        write_scalar(ctx, value, force_redact)
      end

      def write_null(ctx)
        ctx.writer << 'null'
      end

      def write_at_depth_limit(ctx)
        ctx.mark_truncated.call
        ctx.writer << TRUNCATION_MARKER
      end

      def binary_string?(value)
        value.is_a?(String) && value.encoding == Encoding::BINARY
      end

      def write_binary_bytes(ctx, value)
        ctx.writer << %("[bytes:#{value.bytesize}]")
      end

      def write_redacted_string(ctx, value, force_redact)
        redacted = force_redact ? Hypertube::Utils::Logging::LogsRedactor::PLACEHOLDER : Hypertube::Utils::Logging::LogsRedactor.redact_value(value)
        write_string(ctx.writer, redacted, ctx.local_budget, ctx.mark_truncated)
      end

      def write_scalar(ctx, value, force_redact)
        case value
        when TrueClass, FalseClass, Numeric
          ctx.writer << value.to_s
        else
          formatted = force_redact ? Hypertube::Utils::Logging::LogsRedactor::PLACEHOLDER : Hypertube::Utils::Logging::LogsRedactor.redact_value(format_value(value))
          write_string(ctx.writer, formatted, ctx.local_budget, ctx.mark_truncated)
        end
      end

      def format_value(value)
        value.to_s
      rescue StandardError
        "[unrenderable:#{value.class.name}]"
      end
    end
  end
end
