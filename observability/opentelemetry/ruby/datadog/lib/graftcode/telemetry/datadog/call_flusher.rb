# frozen_string_literal: true

require 'graftcode/instrumentation'

module Graftcode
  module Telemetry
    module Datadog
      class CallFlusher
        include Hypertube::Utils::Abstractions::IHypertubeHandling

        def initialize(pipeline)
          @pipeline = pipeline
        end

        def on_process(_context, _command); end

        def on_processed(_context, _response)
          @pipeline.flush
        end

        def on_handling_error(_context, _exception)
          @pipeline.flush
        end
      end
    end
  end
end
