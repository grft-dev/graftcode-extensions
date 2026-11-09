# frozen_string_literal: true

require 'rack/mock'

RSpec.describe Graftcode::Telemetry::Datadog::GraftCodeDatadogRequestTelemetryMiddleware do
  def request(app, path = '/api/fetch/abc', headers = {})
    telemetry = start_telemetry('TestGgRubyWebApi')
    middleware = described_class.new(app, telemetry: telemetry)
    Rack::MockRequest.new(middleware).get(path, headers)
  end

  it 'opens a server span named after the controller' do
    app = lambda do |env|
      env['graft.controller'] = 'Fetch'
      env['sinatra.route'] = 'GET /api/fetch/:input'
      [200, {}, ['ok']]
    end

    request(app)

    span = finished_spans.first
    expect(span.name).to eq('TestGgRubyWebApi.Fetch')
    expect(span.kind).to eq(:server)
    expect(span.attributes).to include(
      'http.method' => 'GET',
      'http.route' => '/api/fetch/:input',
      'http.status_code' => 200,
      'resource.name' => 'TestGgRubyWebApi.Fetch'
    )
  end

  it 'falls back to the method and path' do
    request(->(_env) { [204, {}, []] }, '/health')

    expect(finished_spans.first.name).to eq('TestGgRubyWebApi GET /health')
  end

  it 'continues the incoming W3C trace and makes the span current for the app' do
    trace_id = '0af7651916cd43dd8448eb211c80319c'
    current = nil
    app = lambda do |_env|
      current = OpenTelemetry::Trace.current_span.context.hex_span_id
      [200, {}, []]
    end

    request(app, '/api', 'HTTP_TRACEPARENT' => "00-#{trace_id}-b7ad6b7169203331-01")

    span = finished_spans.first
    expect(span.hex_trace_id).to eq(trace_id)
    expect(span.hex_parent_span_id).to eq('b7ad6b7169203331')
    expect(current).to eq(span.hex_span_id)
  end

  it 'marks server errors and records raised exceptions' do
    expect { request(->(_env) { raise ArgumentError, 'bad input' }) }.to raise_error(ArgumentError)

    span = finished_spans.first
    expect(span.status.code).to eq(OpenTelemetry::Trace::Status::ERROR)
    expect(span.events.map(&:name)).to include('exception')
  end

  it 'resolves Rails controller names without the Controller suffix' do
    stub_const('FetchController', Class.new)

    name = described_class.controller_name('action_controller.instance' => FetchController.new)

    expect(name).to eq('Fetch')
  end
end
