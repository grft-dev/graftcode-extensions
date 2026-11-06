# frozen_string_literal: true

require 'opentelemetry-api'
require 'opentelemetry/sdk'
require 'opentelemetry/exporter/otlp'
require 'hypertube-ruby-sdk/abstractions'
require 'graftcode/instrumentation'

require_relative 'datadog/version'
require_relative 'datadog/connector_options'
require_relative 'datadog/rate_limited_sampler'
require_relative 'datadog/app_logger'
require_relative 'datadog/connector_pipeline'
require_relative 'datadog/rack_middleware'

module Graftcode
  module Telemetry
    module Datadog
      class << self
        attr_reader :active

        def use(options = {})
          opts = options.is_a?(ConnectorOptions) ? options : ConnectorOptions.from_hash(options)
          name = opts.service_name&.strip
          raise ArgumentError, 'service_name is required' if name.nil? || name.empty?

          if active
            return active if active.service_name == name

            raise RuntimeError, "Graftcode Datadog telemetry is already started for #{active.service_name}"
          end

          pipeline = ConnectorPipeline.new(opts)
          telemetry = TelemetryHandle.new(pipeline)
          @active = telemetry
          telemetry
        end

        def shutdown
          active&.shutdown
          @active = nil
        end
      end

      class TelemetryHandle
        attr_reader :service_name, :tracer, :logger, :exporting

        def initialize(pipeline)
          @pipeline = pipeline
          @service_name = pipeline.service_name
          @tracer = pipeline.tracer
          @logger = pipeline.logger
          @exporting = pipeline.exporting
        end

        def flush(timeout_ms: 5000)
          @pipeline.flush(timeout_ms: timeout_ms)
        end

        def shutdown
          @pipeline.close
        end
      end
    end
  end
end
