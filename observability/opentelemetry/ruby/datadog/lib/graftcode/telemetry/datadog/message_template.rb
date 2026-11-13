# frozen_string_literal: true

module Graftcode
  module Telemetry
    module Datadog
      module MessageTemplate
        NULL_VALUE = '(null)'

        RenderState = Struct.new(:template, :arguments, :properties, :builder, :arg_index, :index,
                                 keyword_init: true) do
          def initialize(template, arguments, properties)
            super(
              template: template,
              arguments: arguments,
              properties: properties,
              builder: +'',
              arg_index: 0,
              index: 0
            )
          end
        end

        module_function

        def render(template, args, properties)
          return '' if template.nil?

          state = RenderState.new(template, args || [], properties)
          consume_template(state)
          state.builder
        end

        def consume_template(state)
          consume_character(state) while state.index < state.template.length
        end

        def consume_character(state)
          if escaped_brace_at?(state)
            write_escaped_brace(state)
          elsif (closing = placeholder_closing_index(state))
            write_placeholder(state, closing)
          else
            consume_literal(state)
          end
        end

        def escaped_brace_at?(state)
          character = state.template[state.index]
          brace_character?(character) && state.template[state.index + 1] == character
        end

        def write_escaped_brace(state)
          state.builder << state.template[state.index]
          state.index += 2
        end

        def placeholder_closing_index(state)
          return nil unless state.template[state.index] == '{'

          closing = state.template.index('}', state.index + 1)
          return nil if closing.nil? || state.arg_index >= state.arguments.length

          closing
        end

        def write_placeholder(state, closing)
          value = format(state.arguments[state.arg_index])
          state.arg_index += 1
          state.builder << value
          assign_placeholder_property(state, closing, value)
          state.index = closing + 1
        end

        def assign_placeholder_property(state, closing, value)
          name = property_name(state.template[(state.index + 1)...closing])
          state.properties[name] = value unless name.empty?
        end

        def consume_literal(state)
          state.builder << state.template[state.index]
          state.index += 1
        end

        def brace_character?(character)
          ['{', '}'].include?(character)
        end

        def property_name(placeholder)
          name = placeholder.split(/[,:]/, 2).first.to_s.strip
          name.start_with?('@', '$') ? name[1..] : name
        end

        def format(value)
          return NULL_VALUE if value.nil?

          value.to_s
        rescue StandardError
          value.class.name
        end
      end
    end
  end
end
