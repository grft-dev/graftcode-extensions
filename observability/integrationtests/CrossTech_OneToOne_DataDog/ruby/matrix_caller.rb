# frozen_string_literal: true

require 'json'
require 'bundler/setup'
require 'graftcode/telemetry/datadog'
require 'hypertube-ruby-sdk/sdk/runtime_bridge'
require 'hypertube-ruby-sdk/utils/ws_connection_data'

root = File.expand_path('..', __dir__)
config_path = ARGV[0] || File.join(root, 'matrix-config.json')
config = JSON.parse(File.read(config_path))
hosts = config['hosts']

telemetry = Graftcode::Telemetry::Datadog::GraftCodeDatadogConnector.start('CrossTech.OneToOne.RubyCaller')

hosts.each do |callee_key, callee|
  input = "matrix-ruby-#{callee_key.downcase}"
  span_name = "CrossMatrix.Ruby->#{callee_key}"
  trace_id = ''
  result = ''

  begin
    telemetry.in_span(span_name, kind: :server) do |span|
      trace_id = span.context.hex_trace_id
      telemetry.logger.info(
        'CrossMatrix Caller=Ruby Callee={Callee} Input={Input} TraceId={TraceId}',
        callee_key,
        input,
        trace_id
      )

      factory = Hypertube::Sdk::RuntimeBridge.web_socket(Hypertube::Utils::WsConnectionData.new(callee['ws']))
      runtime = (callee['runtimeArg'] == 'php' ? factory.php : factory.ruby)
      value = runtime
              .get_type(callee['typeName'])
              .invoke_static_method(callee['methodName'], input)
              .execute
              .get_value

      result = value.nil? ? '' : value.to_s
      telemetry.logger.info('CrossMatrix after call Result={Result}', result)
    end
    telemetry.flush

    puts format('OK|Ruby|%s|%s|%s', callee_key, trace_id, result.tr('|', '/'))
  rescue StandardError => e
    puts format('FAIL|Ruby|%s||%s: %s', callee_key, e.class.name, e.message.tr('|', '/'))
  end
end