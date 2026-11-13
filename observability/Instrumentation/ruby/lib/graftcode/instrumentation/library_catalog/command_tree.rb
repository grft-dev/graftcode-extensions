# frozen_string_literal: true

require_relative '../hypertube_sdk'

module Graftcode
  module Instrumentation
    module LibraryCatalog
      module CommandTree
        module_function

        def find(command, command_type)
          matches = []
          collect(command, command_type, matches)
          matches
        end

        def collect(command, command_type, matches)
          matches << command if command.command_type == command_type
          command.payload.each do |item|
            collect(item, command_type, matches) if item.is_a?(Hypertube::Utils::Command)
          end
        end
      end
    end
  end
end
