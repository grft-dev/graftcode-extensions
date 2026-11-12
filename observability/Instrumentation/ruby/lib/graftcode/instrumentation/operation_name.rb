# frozen_string_literal: true

require_relative 'hypertube_sdk'

module Graftcode
  module Instrumentation
    module OperationName
      MAX_DEPTH = 32

      MEMBER_COMMANDS = [
        Hypertube::Utils::CommandType::INVOKE_STATIC_METHOD,
        Hypertube::Utils::CommandType::INVOKE_INSTANCE_METHOD,
        Hypertube::Utils::CommandType::INVOKE_GENERIC_STATIC_METHOD,
        Hypertube::Utils::CommandType::INVOKE_GENERIC_METHOD,
        Hypertube::Utils::CommandType::GET_STATIC_FIELD,
        Hypertube::Utils::CommandType::SET_STATIC_FIELD,
        Hypertube::Utils::CommandType::GET_INSTANCE_FIELD,
        Hypertube::Utils::CommandType::SET_INSTANCE_FIELD,
        Hypertube::Utils::CommandType::GET_STATIC_METHOD_AS_DELEGATE,
        Hypertube::Utils::CommandType::GET_INSTANCE_METHOD_AS_DELEGATE
      ].freeze

      TYPE_FROM_TARGET_COMMANDS = [
        Hypertube::Utils::CommandType::CREATE_CLASS_INSTANCE,
        Hypertube::Utils::CommandType::SET_INSTANCE_FIELD,
        Hypertube::Utils::CommandType::INVOKE_STATIC_METHOD,
        Hypertube::Utils::CommandType::INVOKE_INSTANCE_METHOD,
        Hypertube::Utils::CommandType::GET_STATIC_FIELD,
        Hypertube::Utils::CommandType::GET_INSTANCE_FIELD
      ].freeze

      module_function

      def resolve(command)
        case command.command_type
        when *MEMBER_COMMANDS
          qualified = qualify(
            resolve_type_name_core(payload_at(command, 0), 0),
            resolve_text(payload_at(command, 1), 0)
          )
          qualified || fallback(command)
        when Hypertube::Utils::CommandType::CREATE_CLASS_INSTANCE
          created_type = resolve_type_name_core(payload_at(command, 0), 0)
          created_type ? "#{created_type}.ctor" : fallback(command)
        when Hypertube::Utils::CommandType::INVOKE_GLOBAL_FUNCTION,
             Hypertube::Utils::CommandType::GET_GLOBAL_FIELD
          resolve_text(payload_at(command, 0), 0) || fallback(command)
        else
          fallback(command)
        end
      end

      def resolve_target_type_name(command)
        case command.command_type
        when *MEMBER_COMMANDS, Hypertube::Utils::CommandType::CREATE_CLASS_INSTANCE
          resolve_type_name_core(payload_at(command, 0), 0)
        when Hypertube::Utils::CommandType::GET_TYPE
          resolve_type_name_core(command, 0)
        end
      end

      def blank?(value)
        value.nil? || value.to_s.strip.empty?
      end

      def qualify(type_name, member_name)
        return nil if blank?(member_name)

        blank?(type_name) ? member_name : "#{type_name}.#{member_name}"
      end

      def fallback(command)
        type_name = Hypertube::Utils::CommandType.get_name(command.command_type)
        "hypertube.#{type_name}"
      end

      def payload_at(command, index)
        command.payload[index]
      end

      def resolve_text(payload_item, depth)
        return nil if depth > MAX_DEPTH
        return payload_item.strip if payload_item.is_a?(String) && !payload_item.strip.empty?
        return nil unless payload_item.is_a?(Hypertube::Utils::Command)
        return nil unless payload_item.command_type == Hypertube::Utils::CommandType::VALUE

        resolve_text(payload_at(payload_item, 0), depth + 1)
      end

      def resolve_type_name_core(target, depth)
        return nil if depth > MAX_DEPTH || target.nil?
        return target.strip if target.is_a?(String) && !target.strip.empty?
        return target.name if target.is_a?(Module)
        return resolve_type_name_from_command(target, depth) if target.is_a?(Hypertube::Utils::Command)

        target.class.name
      end

      def resolve_type_name_from_command(command, depth)
        case command.command_type
        when Hypertube::Utils::CommandType::VALUE,
             *TYPE_FROM_TARGET_COMMANDS
          resolve_type_name_core(payload_at(command, 0), depth + 1)
        when Hypertube::Utils::CommandType::GET_TYPE,
             Hypertube::Utils::CommandType::CAST,
             Hypertube::Utils::CommandType::CREATE_NULL
          command.payload.each do |item|
            name = resolve_text(item, depth + 1)
            return name unless blank?(name)
          end
          nil
        end
      end
    end
  end
end
