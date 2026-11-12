# frozen_string_literal: true

require_relative 'hypertube_sdk'
require_relative 'body_capture/constants'
require_relative 'body_capture/write_context'
require_relative 'body_capture/string_writer'
require_relative 'body_capture/value_writer'
require_relative 'body_capture/command_writer'

module Graftcode
  module Instrumentation
    module BodyCapture
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
    end
  end
end
