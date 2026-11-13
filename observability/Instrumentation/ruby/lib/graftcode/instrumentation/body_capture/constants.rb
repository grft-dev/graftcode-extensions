# frozen_string_literal: true

module Graftcode
  module Instrumentation
    module BodyCapture
      TRUNCATION_MARKER = '"__truncated__"'
      MAX_DEPTH = 24
      CLOSING_COST_PER_LEVEL = 18
      SCALAR_RESERVE = 32

      Result = Struct.new(:body, :truncated, keyword_init: true)
    end
  end
end
