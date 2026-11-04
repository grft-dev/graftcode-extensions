# frozen_string_literal: true

require_relative 'instrumentation/version'
require_relative 'instrumentation/telemetry_settings'
require_relative 'instrumentation/operation_name'
require_relative 'instrumentation/library_catalog'
require_relative 'instrumentation/body_capture'
require_relative 'instrumentation/call_span'
require_relative 'instrumentation/graftcode_instrumentation'

module Graftcode
  module Instrumentation
    def self.attach(tracer_provider, minimum_log_level: nil)
      GraftcodeInstrumentation.attach(tracer_provider, minimum_log_level: minimum_log_level)
    end
  end
end
