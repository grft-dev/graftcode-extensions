# frozen_string_literal: true

require_relative 'lib/graftcode/telemetry/datadog/version'

Gem::Specification.new do |spec|
  spec.name = 'graftcode-datadog-connector'
  spec.version = Graftcode::Telemetry::Datadog::VERSION
  spec.authors = ['Graftcode S.A.']
  spec.email = ['support@graftcode.com']
  spec.summary = 'One-call Datadog (OTLP) setup for Ruby services using Graftcode'
  spec.description = 'OpenTelemetry traces, app logging, and Graftcode Hypertube instrumentation exported to Datadog via OTLP.'
  spec.license = 'MIT'
  spec.required_ruby_version = '>= 2.7'

  spec.files = Dir.chdir(__dir__) do
    Dir['lib/**/*.rb', 'README.md']
  end
  spec.require_paths = ['lib']

  # spec.add_runtime_dependency 'graftcode-instrumentation', '~> 1.0'
  spec.add_runtime_dependency 'opentelemetry-api', '~> 1.11'
  spec.add_runtime_dependency 'opentelemetry-sdk', '~> 1.13'
  spec.add_runtime_dependency 'opentelemetry-exporter-otlp', '~> 0.37'
  spec.add_runtime_dependency 'opentelemetry-common', '~> 0.25'

  # spec.add_development_dependency 'hypertube-ruby-sdk', '>= 0.0.1'
  spec.add_development_dependency 'rspec', '~> 3.13'
  spec.add_development_dependency 'rack', '~> 3.1'

  spec.metadata['rubygems_mfa_required'] = 'true'
end
