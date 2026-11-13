# frozen_string_literal: true

RSpec.describe Graftcode::Telemetry::Datadog::MessageTemplate do
  it 'renders placeholders and captures them as properties' do
    properties = {}

    message = described_class.render('{@User} bought {Count,3:D} {{items}}', ['ann', nil], properties)

    expect(message).to eq('ann bought (null) {items}')
    expect(properties).to eq('User' => 'ann', 'Count' => '(null)')
  end

  it 'leaves placeholders without arguments untouched' do
    expect(described_class.render('Hello {Name}', [], {})).to eq('Hello {Name}')
  end
end
