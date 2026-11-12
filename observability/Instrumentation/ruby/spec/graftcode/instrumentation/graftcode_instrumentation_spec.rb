# frozen_string_literal: true

require 'graftcode/context'
require 'hypertube-ruby-sdk/core/interpreter/interpreter'
require_relative 'support/harness'

RSpec.describe Graftcode::Instrumentation::GraftcodeInstrumentation do
  include Graftcode::Instrumentation::Support::Harness::Commands

  let(:harness_class) { Graftcode::Instrumentation::Support::Harness }
  let(:levels) { Hypertube::Utils::Logging::LogsLevel }
  let(:hypertube_logger) { Hypertube::Utils::Logging::HypertubeLogger }
  let(:placeholder) { Hypertube::Utils::Logging::LogsRedactor::PLACEHOLDER }

  around do |example|
    previous = Hypertube::Utils::Logging::HypertubeLogger.minimum_level
    Hypertube::Utils::Logging::HypertubeLogger.minimum_level = Hypertube::Utils::Logging::LogsLevel::OFF
    example.run
  ensure
    Hypertube::Utils::Logging::HypertubeLogger.minimum_level = previous
  end

  it 'hooks into Hypertube on attach and detaches on close' do
    hypertube_logger.minimum_level = levels::ERROR
    harness = harness_class.new(minimum_log_level: levels::INFO)
    expect(hypertube_logger.enabled?(levels::INFO)).to be(true)

    harness.close
    expect(hypertube_logger.enabled?(levels::INFO)).to be(false)
  end

  it 'records logs as span events without a logger, with the exception redacted' do
    harness_class.open do |harness|
      call_span = harness.on_handle(fetch)
      expect(harness.listener.log_enabled?(levels::INFO)).to be(true)
      harness.listener.on_log(levels::INFO, 'Hypertube handling INVOKE_STATIC_METHOD',
                              { 'CommandType' => 'INVOKE_STATIC_METHOD' }, nil)
      harness.listener.on_log(levels::ERROR, 'failed', {}, RuntimeError.new('Password=hunter2'))
      harness.complete(call_span, nil, nil)

      events = harness.finished_spans.first.events
      expect(events[0].name).to eq('Hypertube handling INVOKE_STATIC_METHOD')
      expect(events[0].attributes).to include('CommandType' => 'INVOKE_STATIC_METHOD', 'log.level' => 'Information')
      expect(events[1].name).to eq('exception')
      expect(events[1].attributes['exception.message']).to eq(placeholder)
      events[1].attributes.each_value { |value| expect(value.to_s).not_to include('hunter2') }
    end
  end

  it 'forwards logs to the logger instead of recording them on the span' do
    harness_class.open(minimum_log_level: levels::TRACE) do |harness|
      call_span = harness.on_handle(fetch)
      harness.listener.on_log(levels::WARNING, 'hello ada', { 'Name' => 'ada' }, nil)
      harness.complete(call_span, nil, nil)

      expect(harness.logs.size).to eq(1)
      entry = harness.logs.first
      expect(entry.severity_number).to eq(13)
      expect(entry.severity_text).to eq('Warning')
      expect(entry.body).to eq('hello ada')
      expect(entry.span_id).to eq(call_span.span.context.span_id)
      expect(entry.attributes).to include('Name' => 'ada')
      expect(harness.finished_spans.first.events.to_a).to be_empty
    end
  end

  it 'adds redacted exception details to error records' do
    harness_class.open(minimum_log_level: levels::INFO) do |harness|
      harness.listener.on_log(levels::ERROR, 'failed', {}, ArgumentError.new('Password=hunter2'))

      entry = harness.logs.first
      expect(entry.severity_number).to eq(17)
      expect(entry.severity_text).to eq('Error')
      expect(entry.attributes).to include('exception.type' => 'ArgumentError', 'exception.message' => placeholder)
      expect(entry.attributes['exception.stacktrace']).not_to include("\e[")
    end
  end

  it 'sends a logger at Information only the Hypertube lines at its level' do
    harness_class.open(minimum_log_level: levels::INFO) do |harness|
      hypertube_logger.get_logger.log_with_level(levels::DEBUG, 'Hypertube debug {Detail}', 'x')
      hypertube_logger.get_logger.log_with_level(levels::INFO, 'Hypertube invoking {CommandType}',
                                                 'INVOKE_STATIC_METHOD')

      expect(harness.logs.size).to eq(1)
      expect(harness.logs.first.severity_text).to eq('Information')
      expect(harness.logs.first.body).to eq('Hypertube invoking INVOKE_STATIC_METHOD')
    end
  end

  it 'lets the logger level decide what is enabled' do
    harness_class.open(minimum_log_level: levels::ERROR) do |harness|
      expect(harness.listener.log_enabled?(levels::WARNING)).to be(false)
      expect(harness.listener.log_enabled?(levels::ERROR)).to be(true)
    end
  end

  it 'disables forwarding when the logger is off' do
    hypertube_logger.minimum_level = levels::ERROR
    harness_class.open(minimum_log_level: levels::OFF) do |harness|
      expect(hypertube_logger.enabled?(levels::INFO)).to be(false)
      expect(harness.listener.log_enabled?(levels::ERROR)).to be(false)
    end
  end

  it 'disables logs when nothing records the span' do
    harness_class.open(recording: false) do |harness|
      call_span = harness.on_handle(fetch)

      expect(harness.listener.log_enabled?(levels::ERROR)).to be(false)
      harness.complete(call_span, nil, nil)
    end
  end

  it 'produces an invoke span and a handle span in the same trace for an in-memory call' do
    harness_class.open(minimum_log_level: levels::INFO) do |harness|
      ruby = Hypertube::Utils::RuntimeNameHt::RUBY
      command = Hypertube::Utils::Command.new(
        ruby,
        Hypertube::Utils::CommandType::INVOKE_STATIC_METHOD,
        [Hypertube::Utils::Command.new(ruby, Hypertube::Utils::CommandType::GET_TYPE, ['Math']), 'sqrt', 16]
      )

      response = Hypertube::Core::Interpreter::Interpreter.execute(command, Hypertube::Utils::InMemoryConnectionData.new)

      expect(response.command_type).to eq(Hypertube::Utils::CommandType::VALUE)
      handle, invoke = harness.finished_spans
      expect(harness.finished_spans.size).to eq(2)
      expect(handle.name).to eq('Math.sqrt')
      expect(invoke.name).to eq('Graft Math inmemory')
      expect(handle.hex_trace_id).to eq(invoke.hex_trace_id)
      expect(handle.hex_parent_span_id).to eq(invoke.hex_span_id)

      logs = harness.logs
      expect(logs.size).to eq(4)
      expect(logs[0].body).to eq('Hypertube invoking INVOKE_STATIC_METHOD on the Ruby runtime')
      expect(logs[0].span_id).to eq(invoke.span_id)
      expect(logs[1].span_id).to eq(handle.span_id)
    end
  end
end
