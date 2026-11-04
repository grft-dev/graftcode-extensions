# frozen_string_literal: true

require 'hypertube-ruby-sdk/abstractions'

module Graftcode
  module Instrumentation
    module BodyCapture
      TRUNCATION_MARKER = '"__truncated__"'
      MAX_DEPTH = 24
      CLOSING_COST_PER_LEVEL = 18
      SCALAR_RESERVE = 32

      Result = Struct.new(:body, :truncated, keyword_init: true)

      module_function

      def render(command, max_characters)
        return nil unless command.is_a?(Hypertube::Utils::Command)

        truncated = false
        writer = +''
        write_command(writer, command, 0, max_characters, -> { truncated = true })
        Result.new(
          body: Hypertube::Utils::Logging::LogsRedactor.redact_body(writer),
          truncated: truncated
        )
      end

      def write_command(writer, command, depth, budget, mark_truncated)
        type_name = Hypertube::Utils::CommandType.get_name(command.command_type)
        runtime_name = Hypertube::Utils::RuntimeNameHandler.get_name(command.runtime_name)
        writer << %({"runtime":"#{runtime_name}","type":"#{type_name}","payload":[)

        local_budget = [0, budget - ((depth + 1) * CLOSING_COST_PER_LEVEL) - SCALAR_RESERVE].max
        complete = true
        has_content = false

        command.payload.each_with_index do |item, index|
          if writer.length >= local_budget
            mark_truncated.call
            complete = false
            break
          end

          writer << ',' if has_content
          has_content = true

          sensitive = index.positive? &&
                      command.payload[index - 1].is_a?(String) &&
                      Hypertube::Utils::Logging::LogsRedactor.sensitive_key?(command.payload[index - 1])
          unless write_value(writer, item, depth + 1, budget, local_budget, sensitive, mark_truncated)
            complete = false
            break
          end
        end

        unless complete
          writer << ',' if has_content
          writer << TRUNCATION_MARKER
        end

        writer << ']}'
        complete
      end

      def write_value(writer, value, depth, budget, local_budget, force_redact, mark_truncated)
        if depth >= MAX_DEPTH
          mark_truncated.call
          writer << TRUNCATION_MARKER
          return true
        end

        if value.is_a?(Hypertube::Utils::Command)
          return write_command(writer, value, depth, budget, mark_truncated)
        end

        if value.is_a?(String)
          redacted = force_redact ? Hypertube::Utils::Logging::LogsRedactor::PLACEHOLDER : Hypertube::Utils::Logging::LogsRedactor.redact_value(value)
          return write_string(writer, redacted, local_budget, mark_truncated)
        end

        case value
        when TrueClass, FalseClass
          writer << value.to_s
          true
        when Numeric
          writer << value.to_s
          true
        else
          formatted = force_redact ? Hypertube::Utils::Logging::LogsRedactor::PLACEHOLDER : Hypertube::Utils::Logging::LogsRedactor.redact_value(format_value(value))
          write_string(writer, formatted, local_budget, mark_truncated)
        end
      end

      def format_value(value)
        value.to_s
      rescue StandardError
        "[unrenderable:#{value.class.name}]"
      end

      def write_string(writer, value, budget, mark_truncated)
        writer << '"'
        return writer << '"' if value.nil?

        value.each_char do |character|
          if writer.length >= budget
            mark_truncated.call
            writer << '"'
            return false
          end

          case character
          when '"'
            writer << '\"'
          when '\\'
            writer << '\\\\'
          when "\n"
            writer << '\n'
          when "\r"
            writer << '\r'
          when "\t"
            writer << '\t'
          else
            writer << (character.ord < 32 ? format('\u%04x', character.ord) : character)
          end
        end

        writer << '"'
        true
      end
    end
  end
end
