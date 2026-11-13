# frozen_string_literal: true

require_relative 'hypertube_sdk'
require_relative 'operation_name'
require_relative 'library_catalog/command_tree'
require_relative 'library_catalog/gemspec_version'
require_relative 'library_catalog/path_helpers'
require_relative 'library_catalog/resolution'

module Graftcode
  module Instrumentation
    module LibraryCatalog
      @libraries_by_connection = {}
      @libraries_mutex = Mutex.new

      Library = Struct.new(:name, :version, keyword_init: true)
      NAMESPACE_SEPARATOR = /\.|::/.freeze

      CONNECTION_TYPE_NAMES = {
        Hypertube::Utils::ConnectionType::IN_MEMORY => 'IN_MEMORY',
        Hypertube::Utils::ConnectionType::TCP => 'TCP',
        Hypertube::Utils::ConnectionType::WEB_SOCKET => 'WEB_SOCKET',
        Hypertube::Utils::ConnectionType::HTTP2 => 'HTTP2',
        Hypertube::Utils::ConnectionType::TCP_TLS => 'TCP_TLS',
        Hypertube::Utils::ConnectionType::PLUGIN => 'PLUGIN'
      }.freeze

      module_function

      def reset!
        @libraries_mutex.synchronize { @libraries_by_connection = {} }
        GemspecVersion.reset!
      end

      def record(command, connection_data)
        CommandTree.find(command, Hypertube::Utils::CommandType::LOAD_LIBRARY).each do |load|
          record_load(load, connection_data)
        end
      end

      def describe_target(command, connection_data)
        channel = channel_name(connection_data)
        library = resolve_library_name(command, connection_data)

        return "#{runtime_label(command.runtime_name)} #{channel}" if OperationName.blank?(library)

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
        CONNECTION_TYPE_NAMES.fetch(connection_type, 'UNKNOWN')
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
          version = GemspecVersion.read_cached(path)
          @libraries_by_connection[key][name.downcase] = Library.new(name: name, version: version)
        end
      end

      def connection_key(connection_data)
        "#{connection_data.connection_type}|#{connection_data.hostname}"
      end
    end
  end
end
