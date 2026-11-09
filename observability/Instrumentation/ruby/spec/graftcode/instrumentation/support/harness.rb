# frozen_string_literal: true

require 'opentelemetry/sdk'
require 'opentelemetry/sdk/trace/export'
require 'opentelemetry-logs-sdk'

module Graftcode
  module Instrumentation
    module Support
      class Harness
        attr_reader :listener, :tracer_provider, :logger_provider

        def initialize(recording: true, minimum_log_level: nil)
          @spans = OpenTelemetry::SDK::Trace::Export::InMemorySpanExporter.new
          @tracer_provider = OpenTelemetry::SDK::Trace::TracerProvider.new(
            sampler: recording ? OpenTelemetry::SDK::Trace::Samplers::ALWAYS_ON : OpenTelemetry::SDK::Trace::Samplers::ALWAYS_OFF
          )
          @tracer_provider.add_span_processor(OpenTelemetry::SDK::Trace::Export::SimpleSpanProcessor.new(@spans))

          @logs = OpenTelemetry::SDK::Logs::Export::InMemoryLogRecordExporter.new
          @logger_provider = OpenTelemetry::SDK::Logs::LoggerProvider.new
          @logger_provider.add_log_record_processor(OpenTelemetry::SDK::Logs::Export::SimpleLogRecordProcessor.new(@logs))

          @saved_auto_inject = TelemetrySettings.auto_inject_enabled?
          @saved_capture = TelemetrySettings.body_capture_enabled?
          @saved_max = TelemetrySettings.body_max_characters

          @listener = GraftcodeInstrumentation.attach(
            @tracer_provider,
            minimum_log_level: minimum_log_level,
            logger_provider: @logger_provider
          )
        end

        def self.open(**options)
          harness = new(**options)
          yield harness
        ensure
          harness&.close
        end

        def on_invoke(command, connection_data)
          context = Hypertube::Utils::Abstractions::HypertubeCallContext.new
          listener.on_execute(context, command, connection_data)
          context.items.empty? ? nil : context.items.first
        end

        def on_handle(command, headers = {})
          context = Hypertube::Utils::Abstractions::HypertubeCallContext.new
          handled = command
          unless headers.empty?
            pairs = headers.flat_map { |key, value| [key, value] }
            array = Hypertube::Utils::Command.new(
              command.runtime_name,
              Hypertube::Utils::CommandType::ARRAY,
              pairs
            )
            handled = Hypertube::Utils::Command.new(
              command.runtime_name,
              Hypertube::Utils::CommandType::CONTEXT_METADATA,
              [array, command]
            )
          end

          listener.on_process(context, handled)
          context.items.empty? ? nil : context.items.first
        end

        def complete(span, response, exception)
          span&.complete(response, exception)
        end

        def finished_spans
          @spans.finished_spans
        end

        def logs
          @logs.emitted_log_records
        end

        def close
          listener.close
          @tracer_provider.shutdown
          @logger_provider.shutdown
          TelemetrySettings.auto_inject_enabled = @saved_auto_inject
          TelemetrySettings.body_capture_enabled = @saved_capture
          TelemetrySettings.body_max_characters = @saved_max
          Hypertube::Utils::InMemoryConnectionData.new.set_thread_local_headers({})
        end

        module Commands
          module_function

          def value(value)
            Hypertube::Utils::Command.new(
              Hypertube::Utils::RuntimeNameHt::JVM,
              Hypertube::Utils::CommandType::VALUE,
              [value]
            )
          end

          def of(type, *payload)
            Hypertube::Utils::Command.new(Hypertube::Utils::RuntimeNameHt::JVM, type, payload)
          end

          def fetch
            of(
              Hypertube::Utils::CommandType::INVOKE_STATIC_METHOD,
              of(Hypertube::Utils::CommandType::GET_TYPE, value('SomeLib.Service')),
              value('Fetch')
            )
          end
        end
      end
    end
  end
end
