# frozen_string_literal: true

require 'hypertube-ruby-sdk/abstractions'
require 'opentelemetry/sdk'
require 'graftcode/instrumentation'

RSpec.configure do |config|
  config.before do
    Graftcode::Instrumentation::LibraryCatalog.reset!
    Hypertube::Utils::Logging::HypertubeLogger.detach
    Hypertube::Utils::Abstractions::HypertubeBroker.detach
    Hypertube::Utils::InMemoryConnectionData.new.set_thread_local_headers({})
  end
end
