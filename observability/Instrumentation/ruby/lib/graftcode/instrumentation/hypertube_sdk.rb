# frozen_string_literal: true

module Graftcode
  module Instrumentation
    module HypertubeSdk
      FEATURES = %w[abstractions utils/connection_type utils/runtime_name_handler].freeze
      LOADED_MARKER = '/utils/abstractions/hypertube_broker.rb'

      module_function

      # Graftcode Gateway loads the SDK from its Binaries folder by absolute path, not as a gem.
      # Requiring the gem there would fail under Bundler or load a second copy of the broker and logger.
      def require_features
        root = loaded_root
        FEATURES.each do |feature|
          root.nil? ? require("hypertube-ruby-sdk/#{feature}") : require(File.join(root, feature))
        end
      end

      def loaded_root
        broker = $LOADED_FEATURES.find { |path| path.end_with?(LOADED_MARKER) }
        broker && File.expand_path('../..', File.dirname(broker))
      end
    end

    HypertubeSdk.require_features
  end
end
