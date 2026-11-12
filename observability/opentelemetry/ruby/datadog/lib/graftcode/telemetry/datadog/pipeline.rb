# frozen_string_literal: true

require 'opentelemetry/sdk'
require 'opentelemetry-logs-sdk'
require 'opentelemetry/exporter/otlp'
require 'opentelemetry-exporter-otlp-logs'
require 'graftcode/instrumentation'
require_relative 'call_flusher'
require_relative 'logger'

module Graftcode
  module Telemetry
    module Datadog
      class GraftCodeDatadogConnectorPipeline
        DEFAULT_FLUSH_TIMEOUT = 5
        TRACES_PATH = '/v1/traces'
        LOGS_PATH = '/v1/logs'

        attr_reader :service_name, :endpoint, :tracer, :logger, :tracer_provider, :logger_provider

        def initialize(options, span_exporter: nil, log_record_exporter: nil, &on_close)
          options.validate!
          assign_metadata(options, on_close)
          setup_telemetry(options, span_exporter, log_record_exporter)
          configure_opentelemetry!
          setup_application_services(options)
          at_exit { close }
        end

        def flushes_after_calls?
          !@call_flusher.nil?
        end

        def in_span(name, kind: :internal, attributes: nil, &block)
          tracer.in_span(name, kind: kind, attributes: attributes, &block)
        end

        def flush(timeout = DEFAULT_FLUSH_TIMEOUT)
          @close_mutex.synchronize do
            return true if @closed

            traces = @tracer_provider.force_flush(timeout: timeout)
            logs = @logger_provider.force_flush(timeout: timeout)
            traces == OpenTelemetry::SDK::Trace::Export::SUCCESS && logs == OpenTelemetry::SDK::Logs::Export::SUCCESS
          end
        end

        def close
          @close_mutex.synchronize do
            return if @closed

            @closed = true
          end

          Hypertube::Utils::Abstractions::HypertubeBroker.detach(@call_flusher) unless @call_flusher.nil?
          @instrumentation.close
          @tracer_provider.shutdown(timeout: DEFAULT_FLUSH_TIMEOUT)
          @logger_provider.shutdown(timeout: DEFAULT_FLUSH_TIMEOUT)
          @on_close&.call(self)
        end
        alias shutdown close

        def closed?
          @closed
        end

        private

        def assign_metadata(options, on_close)
          @service_name = options.service_name.strip
          @endpoint = options.resolve_endpoint
          @on_close = on_close
          @close_mutex = Mutex.new
          @closed = false
        end

        def setup_telemetry(options, span_exporter, log_record_exporter)
          resource = build_resource(options)
          @tracer_provider = OpenTelemetry::SDK::Trace::TracerProvider.new(resource: resource)
          @tracer_provider.add_span_processor(span_processor(span_exporter))
          @logger_provider = OpenTelemetry::SDK::Logs::LoggerProvider.new(resource: resource)
          @logger_provider.add_log_record_processor(log_record_processor(log_record_exporter))
        end

        def configure_opentelemetry!
          OpenTelemetry.tracer_provider = @tracer_provider
          OpenTelemetry.logger_provider = @logger_provider
          OpenTelemetry.propagation = OpenTelemetry::Context::Propagation::CompositeTextMapPropagator.compose_propagators(
            [
              OpenTelemetry::Trace::Propagation::TraceContext.text_map_propagator,
              OpenTelemetry::Baggage::Propagation.text_map_propagator
            ]
          )
        end

        def setup_application_services(options)
          @tracer = @tracer_provider.tracer(service_name, VERSION)
          @logger = GraftCodeDatadogLogger.new(
            service_name,
            @logger_provider.logger(name: service_name, version: VERSION),
            options.minimum_log_level,
            options.enable_console_logging ? $stdout : nil
          )
          @instrumentation = Graftcode::Instrumentation.attach(
            @tracer_provider,
            minimum_log_level: options.minimum_log_level,
            logger_provider: @logger_provider
          )
          @call_flusher = CallFlusher.new(self) if options.resolve_flush_after_calls
          Hypertube::Utils::Abstractions::HypertubeBroker.attach(@call_flusher) unless @call_flusher.nil?
        end

        def build_resource(options)
          attributes = { 'service.name' => service_name }
          environment = options.resolve_environment
          version = options.resolve_version
          attributes['deployment.environment.name'] = environment unless environment.nil?
          attributes['service.version'] = version unless version.nil?

          OpenTelemetry::SDK::Resources::Resource.default.merge(
            OpenTelemetry::SDK::Resources::Resource.create(attributes)
          )
        end

        def span_processor(exporter)
          return OpenTelemetry::SDK::Trace::Export::SimpleSpanProcessor.new(exporter) unless exporter.nil?

          OpenTelemetry::SDK::Trace::Export::BatchSpanProcessor.new(
            OpenTelemetry::Exporter::OTLP::Exporter.new(endpoint: "#{endpoint}#{TRACES_PATH}")
          )
        end

        def log_record_processor(exporter)
          return OpenTelemetry::SDK::Logs::Export::SimpleLogRecordProcessor.new(exporter) unless exporter.nil?

          OpenTelemetry::SDK::Logs::Export::BatchLogRecordProcessor.new(
            OpenTelemetry::Exporter::OTLP::Logs::LogsExporter.new(endpoint: "#{endpoint}#{LOGS_PATH}")
          )
        end
      end
    end
  end
end
