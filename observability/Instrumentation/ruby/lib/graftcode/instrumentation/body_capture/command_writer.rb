# frozen_string_literal: true

require_relative '../hypertube_sdk'

module Graftcode
  module Instrumentation
    module BodyCapture
      module_function

      def write_command(writer, command, depth, budget, mark_truncated)
        append_command_header(writer, command)

        local_budget = local_budget_for(depth, budget)
        ctx = WriteContext.new(
          writer: writer,
          depth: depth,
          budget: budget,
          local_budget: local_budget,
          mark_truncated: mark_truncated
        )

        complete, has_content = write_payload(ctx, command)
        finalize_command(writer, has_content, complete)
        complete
      end

      def append_command_header(writer, command)
        type_name = Hypertube::Utils::CommandType.get_name(command.command_type)
        runtime_name = Hypertube::Utils::RuntimeNameHandler.get_name(command.runtime_name)
        writer << %({"runtime":"#{runtime_name}","type":"#{type_name}","payload":[)
      end

      def write_payload(ctx, command)
        has_content = false

        command.payload.each_with_index do |item, index|
          if ctx.over_budget?
            ctx.mark_truncated.call
            return [false, has_content]
          end

          ctx.writer << ',' if has_content
          has_content = true

          item_ctx = ctx.child(depth: ctx.depth + 1, local_budget: ctx.local_budget)
          sensitive = payload_item_sensitive?(command, index)
          return [false, has_content] unless write_value(item_ctx, item, force_redact: sensitive)
        end

        [true, has_content]
      end

      def finalize_command(writer, has_content, complete)
        unless complete
          writer << ',' if has_content
          writer << TRUNCATION_MARKER
        end
        writer << ']}'
      end
    end
  end
end
