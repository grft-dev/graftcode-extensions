# frozen_string_literal: true

require 'graftcode/instrumentation'

module Graftcode
  module Telemetry
    module Datadog
      class GraftCodeDatadogConnectorOptions
        ENDPOINT_VARIABLE = 'OTEL_EXPORTER_OTLP_ENDPOINT'
        ENVIRONMENT_VARIABLE = 'DD_ENV'
        VERSION_VARIABLE = 'DD_VERSION'
        DEFAULT_ENDPOINT = 'http://127.0.0.1:4318'
        HOSTED_RECEIVER = '/Binaries/Ruby/core/receiver/receiver.rb'

        attr_accessor :service_name, :endpoint, :environment, :version, :enable_console_logging, :flush_after_calls
        attr_reader :minimum_log_level

        def initialize(env = ENV)
          @env = env
          @service_name = ''
          @endpoint = nil
          @environment = nil
          @version = nil
          @minimum_log_level = Hypertube::Utils::Logging::LogsLevel::INFO
          @enable_console_logging = true
          @flush_after_calls = nil
        end

        # A runtime hosted by the Hypertube native launcher loads its receiver from the extracted Binaries folder.
        def resolve_flush_after_calls(loaded_features = $LOADED_FEATURES)
          return flush_after_calls unless flush_after_calls.nil?

          loaded_features.any? { |path| path.tr('\\', '/').end_with?(HOSTED_RECEIVER) }
        end

        def minimum_log_level=(level)
          @minimum_log_level = level.is_a?(String) ? Hypertube::Utils::Logging::LogsLevel.parse(level) : level
        end

        def resolve_endpoint
          (present(endpoint) || present(@env[ENDPOINT_VARIABLE]) || DEFAULT_ENDPOINT).chomp('/')
        end

        def resolve_environment
          present(environment) || present(@env[ENVIRONMENT_VARIABLE])
        end

        def resolve_version
          present(version) || present(@env[VERSION_VARIABLE])
        end

        def validate!
          raise ArgumentError, 'service_name must be configured for Datadog telemetry.' if present(service_name).nil?
          raise ArgumentError, 'minimum_log_level must be a Hypertube LogsLevel.' unless minimum_log_level.is_a?(Integer)
        end

        private

        def present(value)
          text = value&.to_s&.strip
          text.nil? || text.empty? ? nil : text
        end
      end
    end
  end
end
