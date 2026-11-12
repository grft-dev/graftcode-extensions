# frozen_string_literal: true

module Graftcode
  module Instrumentation
    module LibraryCatalog
      module GemspecVersion
        @version_by_path = {}
        @version_mutex = Mutex.new

        module_function

        def reset!
          @version_mutex.synchronize { @version_by_path = {} }
        end

        def read_cached(path)
          @version_mutex.synchronize do
            @version_by_path.fetch(path) do
              @version_by_path[path] = read(path)
            end
          end
        end

        def read(path)
          gemspec = if path.end_with?('.gemspec')
                      path
                    else
                      Dir[File.join(File.dirname(path), '*.gemspec')].first
                    end
          return '' if gemspec.nil? || !File.file?(gemspec)

          spec = Gem::Specification.load(gemspec)
          spec&.version.to_s
        rescue StandardError
          ''
        end
      end
    end
  end
end
