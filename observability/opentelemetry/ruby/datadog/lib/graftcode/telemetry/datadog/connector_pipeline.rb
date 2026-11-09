# frozen_string_literal: true

require 'opentelemetry/sdk'
require 'opentelemetry/exporter/otlp'
require 'graftcode/instrumentation'
require_relative 'app_logger'
require_relative 'connector_options'
require_relative 'rate_limited_sampler'

module Graftcode
  module Telemetry
    module Datadog
      class ConnectorPipeline
        MISSING_EXPORT_MESSAGE =
          'Datadog telemetry for {ServiceName} is disabled: no OTLP endpoint, Datadog agent host, or DD_API_KEY was configured. ' \
          'Set OTEL_EXPORTER_OTLP_ENDPOINT, DD_AGENT_HOST, or DD_API_KEY in this process or pass otlp_endpoint / agent_host / api_key.'

        attr_reader :service_name, :tracer, :logger, :exporting

        def initialize(options)
          @options = options
          options.validate!
          @service_name = options.service_name.strip
          @exporting = options.exporting?
          @close_mutex = Mutex.new
          @closed = false
          configure_opentelemetry!
          @tracer = OpenTelemetry.tracer_provider.tracer(@service_name, VERSION)
          @logger = AppLogger.new(@service_name, options.minimum_log_level, console: options.console)
          @instrumentation = attach_graftcode! if options.graftcode_enabled
          warn_missing_export unless @exporting
          register_shutdown_hooks
        end

        def flush(timeout_ms: 5000)
          provider = OpenTelemetry.tracer_provider
          return unless provider.respond_to?(:force_flush)

          provider.force_flush(timeout: timeout_ms / 1000.0)
        end

        def close
          @close_mutex.synchronize do
            return if @closed

            @closed = true
          end

          flush
          @instrumentation&.close
          shutdown_tracer_provider
          unregister_shutdown_hooks
        end

        private

        def shutdown_tracer_provider
          provider = OpenTelemetry.tracer_provider
          provider.shutdown if provider.respond_to?(:shutdown)
        end

        def configure_opentelemetry!
          resource = build_resource
          sampler = build_sampler
          previous_exporter = ENV['OTEL_TRACES_EXPORTER']
          if !@exporting && !@options.span_exporter && !previous_exporter
            ENV['OTEL_TRACES_EXPORTER'] = 'none'
          end

          OpenTelemetry::SDK.configure do |c|
            c.resource = resource
            c.service_name = @service_name
            add_span_processors(c)
          end

          OpenTelemetry.tracer_provider.sampler = sampler if OpenTelemetry.tracer_provider.respond_to?(:sampler=)
        ensure
          if !@exporting && !@options.span_exporter && !previous_exporter
            ENV.delete('OTEL_TRACES_EXPORTER')
          end
        end

        def build_resource
          attributes = { 'service.name' => @service_name }
          env = @options.resolve_environment
          version = @options.resolve_version
          attributes['deployment.environment'] = env unless env.nil? || env.empty?
          attributes['service.version'] = version unless version.nil? || version.empty?
          OpenTelemetry::SDK::Resources::Resource.create(attributes)
        end

        def build_sampler
          return RateLimitedSampler.new(@options.traces_per_second) if @options.traces_per_second

          OpenTelemetry::SDK::Trace::Samplers::ALWAYS_ON
        end

        def add_span_processors(configurator)
          if @options.span_exporter
            configurator.add_span_processor(
              OpenTelemetry::SDK::Trace::Export::SimpleSpanProcessor.new(@options.span_exporter)
            )
            return
          end

          return unless @exporting

          exporter = OpenTelemetry::Exporter::OTLP::Exporter.new(
            endpoint: @options.resolve_otlp_endpoint,
            headers: @options.resolve_otlp_headers
          )
          configurator.add_span_processor(
            OpenTelemetry::SDK::Trace::Export::BatchSpanProcessor.new(exporter)
          )
        end

        def attach_graftcode!
          Graftcode::Instrumentation.attach(
            OpenTelemetry.tracer_provider,
            minimum_log_level: @options.minimum_log_level
          )
        end

        def warn_missing_export
          message = MISSING_EXPORT_MESSAGE.gsub('{ServiceName}', @service_name)
          @logger.warn(message)
          $stderr.puts(message) unless @options.console
        end

        def register_shutdown_hooks
          @at_exit = proc { close }
          at_exit(&@at_exit)
        end

        def unregister_shutdown_hooks
          # Ruby does not support removing at_exit handlers; @closed prevents double work.
        end
      end
    end
  end
end
