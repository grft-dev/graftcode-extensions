# frozen_string_literal: true

require_relative 'lib/graftcode/telemetry/datadog/version'

Gem::Specification.new do |spec|
  spec.name = 'graftcode-telemetry-datadog'
  spec.version = Graftcode::Telemetry::Datadog::VERSION
  spec.authors = ['Graftcode S.A.']
  spec.email = ['support@graftcode.com']
  spec.summary = 'Datadog telemetry for Graftcode (Hypertube) applications in Ruby'
  spec.description = 'Sends Graftcode traces and logs to the Datadog Agent over OTLP, with a Rack request middleware and an application logger.'
  spec.license = 'MIT'
  spec.required_ruby_version = '>= 3.1'

  spec.files = Dir.chdir(__dir__) do
    Dir['lib/**/*.rb', 'README.md']
  end
  spec.require_paths = ['lib']

  spec.add_runtime_dependency 'graftcode-instrumentation', '~> 1.0'
  spec.add_runtime_dependency 'opentelemetry-exporter-otlp', '~> 0.30'
  spec.add_runtime_dependency 'opentelemetry-exporter-otlp-logs', '~> 0.2'
  spec.add_runtime_dependency 'opentelemetry-logs-sdk', '~> 0.4'
  spec.add_runtime_dependency 'opentelemetry-sdk', '~> 1.8'

  spec.add_development_dependency 'hypertube-ruby-sdk', '>= 3.2.0'
  spec.add_development_dependency 'rack', '~> 3.1'
  spec.add_development_dependency 'rspec', '~> 3.13'
  spec.add_development_dependency 'rubocop', '~> 1.92'
  spec.add_development_dependency 'rubocop-performance', '~> 1.27'
  spec.add_development_dependency 'rubocop-rspec', '~> 3.10'

  spec.metadata['rubygems_mfa_required'] = 'true'
end
