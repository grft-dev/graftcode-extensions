# frozen_string_literal: true

require_relative '../hypertube_sdk'
require_relative '../operation_name'

module Graftcode
  module Instrumentation
    module LibraryCatalog
      module_function

      def resolve_library_path(command)
        command.payload.reverse_each do |item|
          path = as_text(item)
          return path unless OperationName.blank?(path)
        end
        nil
      end

      def as_text(payload_item)
        if payload_item.is_a?(Hypertube::Utils::Command) &&
           payload_item.command_type == Hypertube::Utils::CommandType::VALUE &&
           payload_item.payload[0].is_a?(String)
          return payload_item.payload[0]
        end

        payload_item.is_a?(String) ? payload_item : nil
      end

      def file_name_without_extension(path)
        name = File.basename(path)
        dot = name.rindex('.')
        dot.nil? || dot.zero? ? name : name[0...dot]
      end
    end
  end
end
