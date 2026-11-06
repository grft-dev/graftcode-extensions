# frozen_string_literal: true

require 'opentelemetry/sdk'
require 'opentelemetry/sdk/trace/samplers'

module Graftcode
  module Telemetry
    module Datadog
      class RateLimitedSampler
        def initialize(traces_per_second)
          @traces_per_second = traces_per_second.to_f
          @capacity = [@traces_per_second, 1.0].max
          @tokens = @capacity
          @last_refill = Process.clock_gettime(Process::CLOCK_MONOTONIC)
          @mutex = Mutex.new
        end

        def should_sample?(trace_id:, parent_context:, links:, name:, kind:, attributes:)
          decision = try_acquire? ? OpenTelemetry::SDK::Trace::Samplers::Decision::RECORD_AND_SAMPLE : OpenTelemetry::SDK::Trace::Samplers::Decision::DROP
          OpenTelemetry::SDK::Trace::Samplers::Result.new(decision: decision)
        end

        def description
          "RateLimitedSampler{#{@traces_per_second}}"
        end

        private

        def try_acquire?
          @mutex.synchronize do
            now = Process.clock_gettime(Process::CLOCK_MONOTONIC)
            elapsed = now - @last_refill
            @tokens = [@capacity, @tokens + (elapsed * @traces_per_second)].min
            @last_refill = now
            return false if @tokens < 1.0

            @tokens -= 1.0
            true
          end
        end
      end
    end
  end
end
