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
          span, token = start_request_tracing(env, method, path)

          begin
            dispatch_request(env, span)
          rescue Exception => e # rubocop:disable Lint/RescueException
            record_request_failure(span, e)
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
          explicit = explicit_controller_name(env)
          return explicit if explicit

          rails_controller_name(env[RAILS_CONTROLLER_KEY])
        end

        def self.explicit_controller_name(env)
          explicit = env[CONTROLLER_KEY]
          return nil unless explicit.is_a?(String) && !explicit.strip.empty?

          explicit
        end

        def self.rails_controller_name(controller)
          return nil if controller.nil?

          strip_controller_suffix(short_class_name(controller))
        end

        def self.short_class_name(controller)
          controller.class.name.to_s.split('::').last.to_s
        end

        def self.strip_controller_suffix(name)
          return name unless name.end_with?(CONTROLLER_SUFFIX) && name.length > CONTROLLER_SUFFIX.length

          name.delete_suffix(CONTROLLER_SUFFIX)
        end

        private

        def start_request_tracing(env, method, path)
          parent = extract_parent(env)
          span = @telemetry.tracer.start_span(
            self.class.operation_name(@telemetry.service_name, method, path, nil),
            with_parent: parent,
            kind: :server,
            attributes: { 'http.method' => method, 'http.route' => path }
          )
          token = OpenTelemetry::Context.attach(OpenTelemetry::Trace.context_with_span(span))
          [span, token]
        end

        def dispatch_request(env, span)
          status, headers, body = @app.call(env)
          apply_response_status(span, status)
          [status, headers, body]
        end

        def extract_parent(env)
          OpenTelemetry::Trace::Propagation::TraceContext.text_map_propagator.extract(
            env,
            getter: OpenTelemetry::Common::Propagation.rack_env_getter
          )
        end

        def apply_response_status(span, status)
          span.set_attribute('http.status_code', status.to_i)
          span.status = OpenTelemetry::Trace::Status.error if status.to_i >= 500
        end

        def record_request_failure(span, exception)
          span.record_exception(exception)
          span.status = OpenTelemetry::Trace::Status.error(exception.message.to_s)
        end

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
