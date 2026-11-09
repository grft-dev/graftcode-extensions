# frozen_string_literal: true

require_relative 'hypertube_sdk'
require_relative 'operation_name'

module Graftcode
  module Instrumentation
    module LibraryCatalog
      @libraries_by_connection = {}
      @libraries_mutex = Mutex.new
      @version_by_path = {}
      @version_mutex = Mutex.new

      Library = Struct.new(:name, :version, keyword_init: true)
      NAMESPACE_SEPARATOR = /\.|::/

      module_function

      def reset!
        @libraries_mutex.synchronize { @libraries_by_connection = {} }
        @version_mutex.synchronize { @version_by_path = {} }
      end

      def record(command, connection_data)
        find_commands(command, Hypertube::Utils::CommandType::LOAD_LIBRARY).each do |load|
          record_load(load, connection_data)
        end
      end

      def describe_target(command, connection_data)
        channel = channel_name(connection_data)
        library = resolve_library_name(command, connection_data)

        if OperationName.blank?(library)
          return "#{runtime_label(command.runtime_name)} #{channel}"
        end

        version = lookup_version(connection_data, library)
        if OperationName.blank?(version)
          "#{library} #{channel}"
        else
          "#{library} #{version} #{channel}"
        end
      end

      def channel_name(connection_data)
        connection_type_name(connection_data.connection_type).tr('_', '').downcase
      end

      def connection_type_name(connection_type)
        case connection_type
        when Hypertube::Utils::ConnectionType::IN_MEMORY then 'IN_MEMORY'
        when Hypertube::Utils::ConnectionType::TCP then 'TCP'
        when Hypertube::Utils::ConnectionType::WEB_SOCKET then 'WEB_SOCKET'
        when Hypertube::Utils::ConnectionType::HTTP2 then 'HTTP2'
        when Hypertube::Utils::ConnectionType::TCP_TLS then 'TCP_TLS'
        when Hypertube::Utils::ConnectionType::PLUGIN then 'PLUGIN'
        else 'UNKNOWN'
        end
      end

      def runtime_label(runtime_name)
        Hypertube::Utils::RuntimeNameHandler.get_name(runtime_name).capitalize
      end

      def record_load(command, connection_data)
        path = resolve_library_path(command)
        return if OperationName.blank?(path)

        name = file_name_without_extension(path)
        return if OperationName.blank?(name)

        @libraries_mutex.synchronize do
          key = connection_key(connection_data)
          @libraries_by_connection[key] ||= {}
          version = read_version_cached(path)
          @libraries_by_connection[key][name.downcase] = Library.new(name: name, version: version)
        end
      end

      def find_commands(command, command_type)
        matches = []
        collect(command, command_type, matches)
        matches
      end

      def collect(command, command_type, matches)
        matches << command if command.command_type == command_type
        command.payload.each do |item|
          collect(item, command_type, matches) if item.is_a?(Hypertube::Utils::Command)
        end
      end

      def resolve_library_name(command, connection_data)
        find_commands(command, Hypertube::Utils::CommandType::LOAD_LIBRARY).each do |load|
          path = resolve_library_path(load)
          name = OperationName.blank?(path) ? nil : file_name_without_extension(path)
          return name unless OperationName.blank?(name)
        end

        type_name = OperationName.resolve_target_type_name(command)
        return nil if OperationName.blank?(type_name)

        libraries = @libraries_mutex.synchronize { @libraries_by_connection[connection_key(connection_data)] }
        return root_namespace(type_name) if libraries.nil?

        best_match = nil
        libraries.each_value do |library|
          next unless type_of_library?(type_name, library.name)

          best_match = library.name if best_match.nil? || library.name.length > best_match.length
        end

        best_match || root_namespace(type_name)
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
        libraries = @libraries_mutex.synchronize { @libraries_by_connection[connection_key(connection_data)] }
        return nil if libraries.nil?

        libraries[library.downcase]&.version
      end

      def connection_key(connection_data)
        "#{connection_data.connection_type}|#{connection_data.hostname}"
      end

      def resolve_library_path(command)
        command.payload.reverse_each do |item|
          path = as_text(item)
          return path unless OperationName.blank?(path)
        end
        nil
      end

      def as_text(payload_item)
        if payload_item.is_a?(Hypertube::Utils::Command) &&
           payload_item.command_type == Hypertube::Utils::CommandType::VALUE &&
           payload_item.payload[0].is_a?(String)
          return payload_item.payload[0]
        end

        payload_item.is_a?(String) ? payload_item : nil
      end

      def file_name_without_extension(path)
        name = File.basename(path)
        dot = name.rindex('.')
        dot.nil? || dot.zero? ? name : name[0...dot]
      end

      def read_version_cached(path)
        @version_mutex.synchronize do
          @version_by_path.fetch(path) do
            @version_by_path[path] = read_version(path)
          end
        end
      end

      def read_version(path)
        gemspec = if path.end_with?('.gemspec')
                    path
                  else
                    Dir[File.join(File.dirname(path), '*.gemspec')].first
                  end
        return '' if gemspec.nil? || !File.file?(gemspec)

        spec = Gem::Specification.load(gemspec)
        spec&.version&.to_s || ''
      rescue StandardError
        ''
      end
    end
  end
end
