# frozen_string_literal: true

require 'opentelemetry-api'
require_relative '../hypertube_sdk'

module Graftcode
  module Instrumentation
    class CallSpan
      DEPENDENCY_TYPE = 'graft.dependency.type'
      DEPENDENCY_TARGET = 'graft.dependency.target'
      BRIDGE_DEPENDENCY_TYPE = 'Graft'
      REQUEST_BODY = 'hypertube.request.body'
      RESPONSE_BODY = 'hypertube.response.body'
      REQUEST_BODY_TRUNCATED = 'hypertube.request.body.truncated'
      RESPONSE_BODY_TRUNCATED = 'hypertube.response.body.truncated'
      TRACE_PARENT_HEADER = 'traceparent'
      TRACE_STATE_HEADER = 'tracestate'
      TRACE_ID_HEADER = 'traceid'

      SKIP_COMMAND_TYPES = [
        Hypertube::Utils::CommandType::PASS_BY_VALUE,
        Hypertube::Utils::CommandType::REGISTER_FOR_UPDATE,
        Hypertube::Utils::CommandType::VALUE_FOR_UPDATE,
        Hypertube::Utils::CommandType::INVOKE_DELEGATE
      ].freeze

      TRACE_CONTEXT = OpenTelemetry::Trace::Propagation::TraceContext.text_map_propagator
    end
  end
end
