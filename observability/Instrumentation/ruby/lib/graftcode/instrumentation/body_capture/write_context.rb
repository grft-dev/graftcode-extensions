# frozen_string_literal: true

require_relative '../hypertube_sdk'

module Graftcode
  module Instrumentation
    module BodyCapture
      WriteContext = Struct.new(:writer, :depth, :budget, :local_budget, :mark_truncated, keyword_init: true) do
        def child(depth:, local_budget:)
          WriteContext.new(
            writer: writer,
            depth: depth,
            budget: budget,
            local_budget: local_budget,
            mark_truncated: mark_truncated
          )
        end

        def over_budget?
          writer.length >= local_budget
        end
      end

      module_function

      def local_budget_for(depth, budget)
        [0, budget - ((depth + 1) * CLOSING_COST_PER_LEVEL) - SCALAR_RESERVE].max
      end

      def payload_item_sensitive?(command, index)
        index.positive? &&
          command.payload[index - 1].is_a?(String) &&
          Hypertube::Utils::Logging::LogsRedactor.sensitive_key?(command.payload[index - 1])
      end
    end
  end
end
