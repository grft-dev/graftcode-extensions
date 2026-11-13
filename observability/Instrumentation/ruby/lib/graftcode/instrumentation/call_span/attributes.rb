# frozen_string_literal: true

require_relative '../hypertube_sdk'
require_relative '../library_catalog'

module Graftcode
  module Instrumentation
    class CallSpan
      def self.set_if_present(span, key, value)
        span.set_attribute(key, value) unless value.nil?
      end

      def self.apply_invoke_attributes(span, command, connection_data, target, operation)
        apply_invoke_metadata(span, command, connection_data)
        apply_invoke_redacted(span, connection_data, target, operation)
      end

      def self.apply_invoke_metadata(span, command, connection_data)
        span.set_attribute('hypertube.runtime', LibraryCatalog.runtime_label(command.runtime_name))
        span.set_attribute('hypertube.command_type', Hypertube::Utils::CommandType.get_name(command.command_type))
        span.set_attribute('hypertube.connection_type',
                           LibraryCatalog.connection_type_name(connection_data.connection_type))
        span.set_attribute(DEPENDENCY_TYPE, BRIDGE_DEPENDENCY_TYPE)
      end

      def self.apply_invoke_redacted(span, connection_data, target, operation)
        redactor = Hypertube::Utils::Logging::LogsRedactor
        set_if_present(span, DEPENDENCY_TARGET, redactor.redact_value(target))
        set_if_present(span, 'hypertube.operation', redactor.redact_value(operation))
        set_if_present(span, 'hypertube.target',
                       redactor.redact_value(redactor.redact_target(connection_data.hostname)))
      end
    end
  end
end
