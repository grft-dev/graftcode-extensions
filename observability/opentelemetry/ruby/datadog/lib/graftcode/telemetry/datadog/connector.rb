# frozen_string_literal: true

require_relative 'options'
require_relative 'pipeline'

module Graftcode
  module Telemetry
    module Datadog
      module GraftCodeDatadogConnector
        @active = nil
        @active_mutex = Mutex.new

        module_function

        def start(service_name = nil, options: nil, **exporters)
          options = prepare_options(service_name, options)
          yield options if block_given?
          activate_pipeline(options, **exporters)
        end

        def active
          @active_mutex.synchronize { @active }
        end

        def prepare_options(service_name, options)
          options ||= GraftCodeDatadogConnectorOptions.new
          options.service_name = service_name unless service_name.nil?
          options
        end

        def activate_pipeline(options, **exporters)
          @active_mutex.synchronize do
            return resolve_existing_active(options) unless @active.nil?

            @active = create_pipeline(options, **exporters)
          end
        end

        def resolve_existing_active(options)
          return @active if @active.service_name == options.service_name.to_s.strip

          raise ArgumentError, "GraftCode Datadog telemetry is already started for #{@active.service_name}"
        end

        def create_pipeline(options, **exporters)
          GraftCodeDatadogConnectorPipeline.new(options, **exporters) do |pipeline|
            @active_mutex.synchronize { @active = nil if @active.equal?(pipeline) }
          end
        end
      end
    end
  end
end
