# frozen_string_literal: true

require_relative 'support/harness'

RSpec.describe Graftcode::Instrumentation::OperationName do
  include Graftcode::Instrumentation::Support::Harness::Commands

  let(:command_type) { Hypertube::Utils::CommandType }

  it 'qualifies a static method with its type' do
    expect(described_class.resolve(fetch)).to eq('SomeLib.Service.Fetch')
  end

  it 'names an instance method on a created instance after the created type' do
    command = of(
      command_type::INVOKE_INSTANCE_METHOD,
      of(command_type::CREATE_CLASS_INSTANCE, of(command_type::GET_TYPE, 'app.Basket')),
      'add'
    )

    expect(described_class.resolve(command)).to eq('app.Basket.add')
  end

  it 'names a constructor ctor' do
    command = of(command_type::CREATE_CLASS_INSTANCE, of(command_type::GET_TYPE, 'app.Basket'))

    expect(described_class.resolve(command)).to eq('app.Basket.ctor')
  end

  it 'uses the name of a module payload' do
    expect(described_class.resolve(of(command_type::INVOKE_STATIC_METHOD, Math, 'sqrt'))).to eq('Math.sqrt')
  end

  it 'uses the name of a global function' do
    expect(described_class.resolve(of(command_type::INVOKE_GLOBAL_FUNCTION, 'module.run'))).to eq('module.run')
  end

  it 'falls back to the command type for an unknown shape' do
    expect(described_class.resolve(of(command_type::LOAD_LIBRARY, 'x.rb'))).to eq('hypertube.LOAD_LIBRARY')
    expect(described_class.resolve(of(command_type::INVOKE_STATIC_METHOD, 'type'))).to eq('hypertube.INVOKE_STATIC_METHOD')
  end
end
