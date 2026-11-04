# frozen_string_literal: true

require_relative 'support/harness'

RSpec.describe Graftcode::Instrumentation::GraftcodeInstrumentation do
  it 'attaches to Hypertube and detaches on close' do
    previous = Hypertube::Utils::Logging::HypertubeLogger.minimum_level
    Hypertube::Utils::Logging::HypertubeLogger.minimum_level = Hypertube::Utils::Logging::LogsLevel::ERROR

    harness = Graftcode::Instrumentation::Support::Harness.new(
      minimum_log_level: Hypertube::Utils::Logging::LogsLevel::INFO
    )
    expect(Hypertube::Utils::Logging::HypertubeLogger.enabled?(Hypertube::Utils::Logging::LogsLevel::INFO)).to be(true)
    harness.close
    expect(Hypertube::Utils::Logging::HypertubeLogger.enabled?(Hypertube::Utils::Logging::LogsLevel::INFO)).to be(false)
  ensure
    Hypertube::Utils::Logging::HypertubeLogger.minimum_level = previous
  end
end
