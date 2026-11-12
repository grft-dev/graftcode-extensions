# frozen_string_literal: true

module Graftcode
  module Instrumentation
    module BodyCapture
      module_function

      def write_string(writer, value, budget, mark_truncated)
        writer << '"'
        return writer << '"' if value.nil?

        value.each_char do |character|
          if writer.length >= budget
            mark_truncated.call
            writer << '"'
            return false
          end

          writer << escape_character(character)
        end

        writer << '"'
        true
      end

      def escape_character(character)
        case character
        when '"' then '\"'
        when '\\' then '\\\\'
        when "\n" then '\n'
        when "\r" then '\r'
        when "\t" then '\t'
        else
          character.ord < 32 ? format('\u%04x', character.ord) : character
        end
      end
    end
  end
end
