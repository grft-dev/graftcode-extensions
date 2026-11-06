# frozen_string_literal: true

require 'graftcode/telemetry/datadog'

RSpec.configure do |config|
  config.before do
    Graftcode::Instrumentation::LibraryCatalog.reset!
    Hypertube::Utils::Logging::HypertubeLogger.detach
    Hypertube::Utils::Abstractions::HypertubeBroker.detach
    Graftcode::Telemetry::Datadog.shutdown
  end
end
