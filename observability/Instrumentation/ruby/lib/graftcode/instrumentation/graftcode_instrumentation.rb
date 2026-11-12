# frozen_string_literal: true

require 'opentelemetry-api'
require_relative 'hypertube_sdk'
require_relative 'call_span'
require_relative 'graftcode_instrumentation/log_recording'

module Graftcode
  module Instrumentation
    class GraftcodeInstrumentation
      include Hypertube::Utils::Abstractions::IHypertubeInvocation
      include Hypertube::Utils::Abstractions::IHypertubeHandling
      include Hypertube::Utils::Logging::IHypertubeLogging
      include LogRecording

      INSTRUMENTATION_NAME = 'Graftcode'
      LOGGER_NAME = 'Hypertube'

      def self.attach(tracer_provider, minimum_log_level: nil, logger_provider: nil)
        new(tracer_provider, minimum_log_level, logger_provider)
      end

      def initialize(tracer_provider, minimum_log_level = nil, logger_provider = nil)
        raise ArgumentError, 'tracer_provider' if tracer_provider.nil?

        @tracer = tracer_provider.tracer(INSTRUMENTATION_NAME, VERSION)
        @minimum_log_level = minimum_log_level
        @logger = if minimum_log_level.nil? || logger_provider.nil?
                    nil
                  else
                    logger_provider.logger(name: LOGGER_NAME, version: VERSION)
                  end

        Hypertube::Utils::Abstractions::HypertubeBroker.attach(self)
        Hypertube::Utils::Logging::HypertubeLogger.attach(self)
      end

      def close
        Hypertube::Utils::Abstractions::HypertubeBroker.detach(self)
        Hypertube::Utils::Logging::HypertubeLogger.detach(self)
      end

      def on_execute(context, command, connection_data)
        reported = Hypertube::Utils::Logging::LogsCommandResolver.resolve_for_invoke(command)
        span = CallSpan.start_invoke(@tracer, reported, connection_data)
        context.items << span unless span.nil?
      end

      def on_response(context, response)
        CallSpan.complete_spans(context, response, nil)
      end

      def on_invocation_error(context, exception)
        CallSpan.complete_spans(context, nil, exception)
      end

      def on_process(context, command)
        headers = {}
        reported = Hypertube::Utils::Logging::LogsCommandResolver.resolve_for_handle(command, headers)
        span = CallSpan.start_handle(@tracer, reported, headers)
        context.items << span unless span.nil?
      end

      def on_processed(context, response)
        CallSpan.complete_spans(context, response, nil)
      end

      def on_handling_error(context, exception)
        CallSpan.complete_spans(context, nil, exception)
      end

      def requested?
        return current_span.recording? if @minimum_log_level.nil?

        @minimum_log_level != Hypertube::Utils::Logging::LogsLevel::OFF
      end

      def on_log(level, message, properties, exception)
        return unless log_enabled?(level)

        if @logger.nil?
          record_span_event(level, message, properties, exception)
        else
          emit_log_record(level, message, properties, exception)
        end
      end

      def log_enabled?(level)
        return current_span.recording? if @minimum_log_level.nil?
        return false if level == Hypertube::Utils::Logging::LogsLevel::OFF
        return false if @minimum_log_level == Hypertube::Utils::Logging::LogsLevel::OFF

        Hypertube::Utils::Logging::LogsLevel.at_least?(level, @minimum_log_level)
      end

      private

      def current_span
        OpenTelemetry::Trace.current_span
      end
    end
  end
end
