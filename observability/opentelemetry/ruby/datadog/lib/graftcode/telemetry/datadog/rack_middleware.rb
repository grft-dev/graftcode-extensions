# frozen_string_literal: true

require 'opentelemetry/sdk'
require 'opentelemetry/common/propagation'

module Graftcode
  module Telemetry
    module Datadog
      module RackEnvGetter
        module_function

        def keys(carrier)
          carrier.keys
        end

        def get(carrier, key)
          carrier["HTTP_#{key.tr('-', '_').upcase}"]
        end
      end

      class RackMiddleware
        def initialize(app, telemetry)
          @app = app
          @telemetry = telemetry
        end

        def call(env)
          request = Rack::Request.new(env)
          parent = OpenTelemetry.propagation.extract(env, getter: RackEnvGetter)
          span = @telemetry.tracer.start_span(
            operation_name(request),
            with_parent: parent,
            kind: :server,
            attributes: {
              'http.method' => request.request_method,
              'http.route' => request.path_info
            }
          )

          token = OpenTelemetry::Context.attach(OpenTelemetry::Trace.context_with_span(span))
          begin
            status, headers, body = @app.call(env)
            span.set_attribute('http.status_code', status)
            span.status = OpenTelemetry::Trace::Status.error if status >= 500
            [status, headers, body]
          ensure
            OpenTelemetry::Context.detach(token)
            span.finish
          end
        end

        private

        def operation_name(request)
          segment = request.path_info.split('/').reject(&:empty?).first
          segment ? "#{@telemetry.service_name}.#{segment}" : "#{@telemetry.service_name} #{request.request_method}"
        end
      end
    end
  end
end
