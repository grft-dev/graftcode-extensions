# frozen_string_literal: true

module Graftcode
  module Telemetry
    module Datadog
      module MessageTemplate
        NULL_VALUE = '(null)'

        module_function

        def render(template, args, properties)
          return '' if template.nil?

          values = args || []
          builder = +''
          arg_index = 0
          index = 0

          while index < template.length
            character = template[index]

            if ['{', '}'].include?(character) && template[index + 1] == character
              builder << character
              index += 2
              next
            end

            closing = character == '{' ? template.index('}', index + 1) : nil
            if closing.nil? || arg_index >= values.length
              builder << character
              index += 1
              next
            end

            value = format(values[arg_index])
            arg_index += 1
            builder << value

            name = property_name(template[(index + 1)...closing])
            properties[name] = value unless name.empty?

            index = closing + 1
          end

          builder
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
