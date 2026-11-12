# frozen_string_literal: true

require_relative 'support/harness'

module Graftcode
  module Instrumentation
    module Support
      class HostileDto
        def to_s
          raise 'to_s exploded'
        end
      end
    end
  end
end

RSpec.describe Graftcode::Instrumentation::BodyCapture do
  include Graftcode::Instrumentation::Support::Harness::Commands

  let(:harness_class) { Graftcode::Instrumentation::Support::Harness }
  let(:settings) { Graftcode::Instrumentation::TelemetrySettings }
  let(:call_span) { Graftcode::Instrumentation::CallSpan }
  let(:invoke_static) { Hypertube::Utils::CommandType::INVOKE_STATIC_METHOD }

  def capturing(max_characters = nil)
    harness_class.open do |harness|
      settings.body_capture_enabled = true
      settings.body_max_characters = max_characters unless max_characters.nil?
      yield harness
    end
  end

  def invoke(harness, command, response)
    harness.complete(harness.on_invoke(command, Hypertube::Utils::InMemoryConnectionData.new), response, nil)
  end

  def tag(harness, key)
    harness.finished_spans.first.attributes[key]
  end

  it 'is disabled by default' do
    expect(settings.body_capture_enabled?).to be(false)
  end

  it 'clamps the body limit' do
    expect(settings.clamp_body_max_characters(1)).to eq(settings::MINIMUM_BODY_MAX_CHARS)
    expect(settings.clamp_body_max_characters((2**31) - 1)).to eq(settings::MAXIMUM_BODY_MAX_CHARS)
  end

  it 'attaches no body while capture is disabled' do
    harness_class.open do |harness|
      invoke(harness, fetch, value('ok'))

      expect(tag(harness, call_span::REQUEST_BODY)).to be_nil
      expect(tag(harness, call_span::RESPONSE_BODY)).to be_nil
    end
  end

  it 'attaches the request and response bodies while capture is enabled' do
    capturing do |harness|
      invoke(harness, fetch, value('order-4711'))

      expect(tag(harness, call_span::REQUEST_BODY)).to include('Fetch')
      expect(tag(harness, call_span::RESPONSE_BODY)).to include('order-4711')
    end
  end

  it 'attaches the request body to the handle span' do
    capturing do |harness|
      harness.complete(harness.on_handle(fetch), nil, nil)

      expect(tag(harness, call_span::REQUEST_BODY)).to include('Fetch')
    end
  end

  it 'redacts sensitive values' do
    capturing do |harness|
      invoke(harness, of(invoke_static, 'password', 'hunter2', 'note', 'Server=x;Password=s3cr3t'), nil)

      body = tag(harness, call_span::REQUEST_BODY)
      expect(body).to include(Hypertube::Utils::Logging::LogsRedactor::PLACEHOLDER)
      expect(body).not_to include('hunter2')
      expect(body).not_to include('s3cr3t')
    end
  end

  it 'flags an oversized payload as truncated' do
    capturing(256) do |harness|
      invoke(harness, of(invoke_static, *Array.new(64, 'y' * 64)), nil)

      expect(tag(harness, call_span::REQUEST_BODY_TRUNCATED)).to be(true)
      expect(tag(harness, call_span::REQUEST_BODY)).to include(described_class::TRUNCATION_MARKER)
    end
  end

  it 'skips rendering for an unrecorded span' do
    harness_class.open(recording: false) do |harness|
      settings.body_capture_enabled = true

      scope = harness.on_invoke(fetch, Hypertube::Utils::InMemoryConnectionData.new)
      harness.complete(scope, value('ok'), nil)

      expect(scope.span).not_to be_recording
      expect(harness.finished_spans).to be_empty
    end
  end

  it 'renders a payload whose to_s raises as a placeholder' do
    capturing do |harness|
      invoke(harness, of(invoke_static, 'Fetch', Graftcode::Instrumentation::Support::HostileDto.new), nil)

      body = tag(harness, call_span::REQUEST_BODY)
      expect(body).to include('Fetch')
      expect(body).to include('[unrenderable:Graftcode::Instrumentation::Support::HostileDto]')
    end
  end

  it 'renders binary strings as their length' do
    capturing do |harness|
      invoke(harness, of(invoke_static, "\x00".b * 42), nil)

      expect(tag(harness, call_span::REQUEST_BODY)).to include('[bytes:42]')
    end
  end

  it 'renders nil as null' do
    capturing do |harness|
      invoke(harness, of(invoke_static, 'Fetch', nil), nil)

      expect(tag(harness, call_span::REQUEST_BODY)).to include('"Fetch",null]')
    end
  end
end
