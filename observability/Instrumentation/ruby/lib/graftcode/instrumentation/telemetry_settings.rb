# frozen_string_literal: true

module Graftcode
  module Instrumentation
    module TelemetrySettings
      MINIMUM_BODY_MAX_CHARS = 256
      DEFAULT_BODY_MAX_CHARS = 8192
      MAXIMUM_BODY_MAX_CHARS = 32_768

      module_function

      def auto_inject_enabled?
        @auto_inject = read_flag('HYPERTUBE_TELEMETRY_AUTO_INJECT', true) if @auto_inject.nil?
        @auto_inject
      end

      def auto_inject_enabled=(value)
        @auto_inject = value
      end

      def body_capture_enabled?
        @capture_bodies = read_flag('HYPERTUBE_TELEMETRY_CAPTURE_BODIES', false) if @capture_bodies.nil?
        @capture_bodies
      end

      def body_capture_enabled=(value)
        @capture_bodies = value
      end

      def body_max_characters
        @body_max_characters = read_body_max_chars if @body_max_characters.nil?
        @body_max_characters
      end

      def body_max_characters=(value)
        @body_max_characters = clamp_body_max_characters(value)
      end

      def clamp_body_max_characters(value)
        return MINIMUM_BODY_MAX_CHARS if value < MINIMUM_BODY_MAX_CHARS

        [value, MAXIMUM_BODY_MAX_CHARS].min
      end

      def read_flag(name, default_value)
        value = ENV.fetch(name, nil)&.strip&.downcase
        return default_value if value.nil? || value.empty?

        value != 'false' && value != '0'
      end

      def read_body_max_chars
        raw = ENV.fetch('HYPERTUBE_TELEMETRY_BODY_MAX_CHARS', nil)
        return DEFAULT_BODY_MAX_CHARS if raw.nil? || raw.strip.empty?

        clamp_body_max_characters(Integer(raw))
      rescue ArgumentError
        DEFAULT_BODY_MAX_CHARS
      end
    end
  end
end
