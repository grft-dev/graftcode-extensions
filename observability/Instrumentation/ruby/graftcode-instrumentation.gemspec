# frozen_string_literal: true

require_relative 'lib/graftcode/instrumentation/version'

Gem::Specification.new do |spec|
  spec.name = 'graftcode-instrumentation'
  spec.version = Graftcode::Instrumentation::VERSION
  spec.authors = ['Graftcode S.A.']
  spec.email = ['support@graftcode.com']
  spec.summary = 'OpenTelemetry instrumentation for Graftcode (Hypertube) calls in Ruby'
  spec.description = 'Client and server spans, W3C trace context propagation, and Hypertube log forwarding for Ruby.'
  spec.license = 'MIT'
  spec.required_ruby_version = '>= 2.7'

  spec.files = Dir.chdir(__dir__) do
    Dir['lib/**/*.rb', 'README.md']
  end
  spec.require_paths = ['lib']

  spec.add_runtime_dependency 'opentelemetry-api', '~> 1.4'

  spec.add_development_dependency 'graftcode-context', '~> 1.0'
  spec.add_development_dependency 'hypertube-ruby-sdk', '>= 0.0.1'
  spec.add_development_dependency 'opentelemetry-logs-sdk', '~> 0.4'
  spec.add_development_dependency 'opentelemetry-sdk', '~> 1.8'
  spec.add_development_dependency 'rspec', '~> 3.13'

  spec.metadata['rubygems_mfa_required'] = 'true'
end
