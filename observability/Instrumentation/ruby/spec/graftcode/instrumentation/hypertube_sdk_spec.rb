# frozen_string_literal: true

require_relative 'support/harness'

RSpec.describe Graftcode::Instrumentation::HypertubeSdk do
  it 'resolves the root of the Hypertube SDK that is already loaded' do
    root = described_class.loaded_root

    expect(root).not_to be_nil
    expect(File).to exist(File.join(root, 'abstractions.rb'))
    expect($LOADED_FEATURES).to include(File.join(root, 'utils', 'abstractions', 'hypertube_broker.rb'))
  end
end
