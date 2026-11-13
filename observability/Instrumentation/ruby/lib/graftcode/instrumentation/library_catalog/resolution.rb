# frozen_string_literal: true

require_relative '../hypertube_sdk'
require_relative '../operation_name'
require_relative 'command_tree'

module Graftcode
  module Instrumentation
    module LibraryCatalog
      module_function

      def resolve_library_name(command, connection_data)
        name = library_name_from_load_commands(command)
        return name if name

        type_name = OperationName.resolve_target_type_name(command)
        return nil if OperationName.blank?(type_name)

        libraries = libraries_for(connection_data)
        return root_namespace(type_name) if libraries.nil?

        match_library_for_type(type_name, libraries) || root_namespace(type_name)
      end

      def library_name_from_load_commands(command)
        CommandTree.find(command, Hypertube::Utils::CommandType::LOAD_LIBRARY).each do |load|
          path = resolve_library_path(load)
          next if OperationName.blank?(path)

          name = file_name_without_extension(path)
          return name unless OperationName.blank?(name)
        end
        nil
      end

      def libraries_for(connection_data)
        @libraries_mutex.synchronize { @libraries_by_connection[connection_key(connection_data)] }
      end

      def match_library_for_type(type_name, libraries)
        libraries.each_value.reduce(nil) do |best, library|
          next best unless type_of_library?(type_name, library.name)

          best.nil? || library.name.length > best.length ? library.name : best
        end
      end

      def root_namespace(type_name)
        separator = type_name.index(NAMESPACE_SEPARATOR)
        separator.nil? ? type_name : type_name[0...separator]
      end

      def type_of_library?(type_name, library_name)
        type = type_name.downcase
        library = library_name.downcase
        type == library || type.start_with?("#{library}.", "#{library}::")
      end

      def lookup_version(connection_data, library)
        libraries = libraries_for(connection_data)
        return nil if libraries.nil?

        libraries[library.downcase]&.version
      end
    end
  end
end
