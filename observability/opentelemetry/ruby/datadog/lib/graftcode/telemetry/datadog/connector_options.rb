# frozen_string_literal: true

module Graftcode
  module Telemetry
    module Datadog
      class ConnectorOptions
        AGENT_OTLP_HTTP_PORT = 4318
        DEFAULT_LOG_LEVEL = Hypertube::Utils::Logging::LogsLevel::INFO

        attr_accessor :service_name,
                      :otlp_endpoint,
                      :otlp_headers,
                      :agent_host,
                      :api_key,
                      :site,
                      :environment,
                      :version,
                      :traces_per_second,
                      :minimum_log_level,
                      :console,
                      :graftcode_enabled,
                      :span_exporter

        def initialize
          @service_name = nil
          @otlp_endpoint = nil
          @otlp_headers = {}
          @agent_host = nil
          @api_key = nil
          @site = nil
          @environment = nil
          @version = nil
          @traces_per_second = nil
          @minimum_log_level = DEFAULT_LOG_LEVEL
          @console = true
          @graftcode_enabled = true
          @span_exporter = nil
        end

        def self.from_hash(options)
          opts = new
          return opts if options.nil?

          opts.service_name = options[:service_name] || options['service_name']
          opts.otlp_endpoint = options[:otlp_endpoint] || options['otlp_endpoint']
          opts.otlp_headers = (options[:otlp_headers] || options['otlp_headers'] || {}).dup
          opts.agent_host = options[:agent_host] || options['agent_host']
          opts.api_key = options[:api_key] || options['api_key']
          opts.site = options[:site] || options['site']
          opts.environment = options[:environment] || options['environment']
          opts.version = options[:version] || options['version']
          opts.traces_per_second = options[:traces_per_second] || options['traces_per_second']
          opts.minimum_log_level = parse_log_level(options[:log_level] || options['log_level'])
          opts.console = options.fetch(:console, options.fetch('console', true))
          graftcode = options[:graftcode]
          graftcode = options['graftcode'] if graftcode.nil?
          opts.graftcode_enabled = graftcode != false
          opts.span_exporter = options[:span_exporter] || options['span_exporter']
          opts
        end

        def validate!
          raise ArgumentError, 'service_name is required' if blank?(service_name)

          if traces_per_second && traces_per_second <= 0
            raise ArgumentError, 'traces_per_second must be greater than zero'
          end
        end

        def exporting?
          return true if span_exporter

          resolve_otlp_endpoint
          true
        rescue ExportNotConfigured
          false
        end

        def resolve_otlp_endpoint
          return test_endpoint if span_exporter

          explicit = read_env('OTEL_EXPORTER_OTLP_TRACES_ENDPOINT') ||
                     read_env('OTEL_EXPORTER_OTLP_ENDPOINT') ||
                     normalize_endpoint(otlp_endpoint)
          return append_traces_path(explicit) unless blank?(explicit)

          host = read_env('DD_AGENT_HOST') || agent_host
          return "http://#{host}:#{AGENT_OTLP_HTTP_PORT}/v1/traces" unless blank?(host)

          key = read_env('DD_API_KEY') || api_key
          unless blank?(key)
            return "https://otlp.#{datadog_site}/v1/traces"
          end

          raise ExportNotConfigured
        end

        def resolve_otlp_headers
          headers = otlp_headers.dup
          key = read_env('DD_API_KEY') || api_key
          headers['dd-api-key'] = key unless blank?(key) || headers.key?('dd-api-key')
          headers
        end

        def resolve_environment
          read_env('DD_ENV') || environment
        end

        def resolve_version
          read_env('DD_VERSION') || version
        end

        def datadog_site
          site_value = read_env('DD_SITE') || site
          blank?(site_value) ? 'datadoghq.com' : site_value
        end

        class ExportNotConfigured < StandardError
        end

        private

        def test_endpoint
          'http://127.0.0.1:9/v1/traces'
        end

        def read_env(name)
          value = ENV.fetch(name, nil)
          blank?(value) ? nil : value.strip
        end

        def blank?(value)
          value.nil? || value.to_s.strip.empty?
        end

        def normalize_endpoint(value)
          blank?(value) ? nil : value.strip
        end

        def append_traces_path(endpoint)
          return endpoint if endpoint.include?('/v1/traces')

          endpoint.end_with?('/') ? "#{endpoint}v1/traces" : "#{endpoint}/v1/traces"
        end

        def self.parse_log_level(raw)
          return DEFAULT_LOG_LEVEL if raw.nil?

          Hypertube::Utils::Logging::LogsLevel.parse(raw.to_s) || DEFAULT_LOG_LEVEL
        end
      end
    end
  end
end
