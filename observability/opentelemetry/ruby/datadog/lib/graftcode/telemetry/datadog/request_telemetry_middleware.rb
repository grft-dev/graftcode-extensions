# frozen_string_literal: true

require 'opentelemetry/sdk'

module Graftcode
  module Telemetry
    module Datadog
      class GraftCodeDatadogRequestTelemetryMiddleware
        CONTROLLER_KEY = 'graft.controller'
        CONTROLLER_SUFFIX = 'Controller'
        RAILS_CONTROLLER_KEY = 'action_controller.instance'
        SINATRA_ROUTE_KEY = 'sinatra.route'
        RESOURCE_NAME = 'resource.name'

        def initialize(app, telemetry:)
          raise ArgumentError, 'telemetry' if telemetry.nil?

          @app = app
          @telemetry = telemetry
        end

        def call(env)
          method = env['REQUEST_METHOD'].to_s
          path = resolve_path(env)
          parent = OpenTelemetry::Trace::Propagation::TraceContext.text_map_propagator.extract(
            env,
            getter: OpenTelemetry::Common::Propagation.rack_env_getter
          )

          span = @telemetry.tracer.start_span(
            self.class.operation_name(@telemetry.service_name, method, path, nil),
            with_parent: parent,
            kind: :server,
            attributes: { 'http.method' => method, 'http.route' => path }
          )
          token = OpenTelemetry::Context.attach(OpenTelemetry::Trace.context_with_span(span))

          begin
            status, headers, body = @app.call(env)
            span.set_attribute('http.status_code', status.to_i)
            span.status = OpenTelemetry::Trace::Status.error if status.to_i >= 500
            [status, headers, body]
          rescue Exception => e # rubocop:disable Lint/RescueException
            span.record_exception(e)
            span.status = OpenTelemetry::Trace::Status.error(e.message.to_s)
            raise
          ensure
            complete(span, env, method, path)
            OpenTelemetry::Context.detach(token)
          end
        end

        def self.operation_name(service_name, method, path, controller)
          return "#{service_name}.#{controller}" unless controller.nil? || controller.strip.empty?

          "#{service_name} #{method} #{path.nil? || path.empty? ? '/' : path}"
        end

        def self.controller_name(env)
          explicit = env[CONTROLLER_KEY]
          return explicit if explicit.is_a?(String) && !explicit.strip.empty?

          controller = env[RAILS_CONTROLLER_KEY]
          return nil if controller.nil?

          name = controller.class.name.to_s.split('::').last.to_s
          name.end_with?(CONTROLLER_SUFFIX) && name.length > CONTROLLER_SUFFIX.length ? name.delete_suffix(CONTROLLER_SUFFIX) : name
        end

        private

        def complete(span, env, method, path)
          route = route_template(env) || path
          name = self.class.operation_name(@telemetry.service_name, method, route, self.class.controller_name(env))

          span.set_attribute('http.route', route)
          span.set_attribute(RESOURCE_NAME, name)
          span.name = name
          span.finish
        end

        def route_template(env)
          route = env[SINATRA_ROUTE_KEY]
          return nil unless route.is_a?(String)

          route.split(' ', 2).last
        end

        def resolve_path(env)
          path = "#{env['SCRIPT_NAME']}#{env['PATH_INFO']}"
          path.empty? ? '/' : path
        end
      end
    end
  end
end
