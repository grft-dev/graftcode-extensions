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
          options ||= GraftCodeDatadogConnectorOptions.new
          options.service_name = service_name unless service_name.nil?
          yield options if block_given?

          @active_mutex.synchronize do
            unless @active.nil?
              return @active if @active.service_name == options.service_name.to_s.strip

              raise ArgumentError, "GraftCode Datadog telemetry is already started for #{@active.service_name}"
            end

            @active = GraftCodeDatadogConnectorPipeline.new(options, **exporters) do |pipeline|
              @active_mutex.synchronize { @active = nil if @active.equal?(pipeline) }
            end
          end
        end

        def active
          @active_mutex.synchronize { @active }
        end
      end
    end
  end
end
