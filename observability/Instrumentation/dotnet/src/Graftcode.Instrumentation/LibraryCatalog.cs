using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Reflection;
using Hypertube.Netcore.Utils;
using Hypertube.Netcore.Utils.ConnectionData;

namespace Graftcode.Instrumentation
{
    internal static class LibraryCatalog
    {
        private static readonly ConcurrentDictionary<string, ConcurrentDictionary<string, string>> LibrariesByConnection = new();
        private static readonly ConcurrentDictionary<string, string> VersionByPath = new();

        internal static void Record(ICommand command, IConnectionData connectionData)
        {
            foreach (var load in FindCommands(command, CommandType.LoadLibrary))
            {
                RecordLoad(load, connectionData);
            }
        }

        internal static string DescribeTarget(ICommand command, IConnectionData connectionData)
        {
            var channel = connectionData.ConnectionType.ToString().ToLowerInvariant();
            var library = ResolveLibraryName(command, connectionData);

            if (string.IsNullOrWhiteSpace(library))
            {
                return $"{command.RuntimeName} {channel}";
            }

            var version = LookupVersion(connectionData, library);
            return string.IsNullOrWhiteSpace(version)
                ? $"{library} {channel}"
                : $"{library} {version} {channel}";
        }

        private static void RecordLoad(ICommand command, IConnectionData connectionData)
        {
            var path = ResolveLibraryPath(command);
            if (string.IsNullOrWhiteSpace(path))
            {
                return;
            }

            var name = Path.GetFileNameWithoutExtension(path);
            if (string.IsNullOrWhiteSpace(name))
            {
                return;
            }

            var libraries = LibrariesByConnection.GetOrAdd(
                ConnectionKey(connectionData),
                _ => new ConcurrentDictionary<string, string>(StringComparer.OrdinalIgnoreCase));

            libraries[name] = VersionByPath.GetOrAdd(path, ReadVersion);
        }

        private static IEnumerable<ICommand> FindCommands(ICommand command, CommandType commandType)
        {
            if (command.CommandType == commandType)
            {
                yield return command;
            }

            foreach (var item in command.Payload)
            {
                if (item is ICommand inner)
                {
                    foreach (var match in FindCommands(inner, commandType))
                    {
                        yield return match;
                    }
                }
            }
        }

        private static string? ResolveLibraryName(ICommand command, IConnectionData connectionData)
        {
            foreach (var load in FindCommands(command, CommandType.LoadLibrary))
            {
                var path = ResolveLibraryPath(load);
                var name = string.IsNullOrWhiteSpace(path) ? null : Path.GetFileNameWithoutExtension(path);
                if (!string.IsNullOrWhiteSpace(name))
                {
                    return name;
                }
            }

            var typeName = OperationName.ResolveTargetTypeName(command);
            if (string.IsNullOrWhiteSpace(typeName))
            {
                return null;
            }

            if (!LibrariesByConnection.TryGetValue(ConnectionKey(connectionData), out var libraries))
            {
                return RootNamespace(typeName);
            }

            string? bestMatch = null;
            foreach (var name in libraries.Keys)
            {
                if (!IsTypeOfLibrary(typeName, name))
                {
                    continue;
                }

                if (bestMatch == null || name.Length > bestMatch.Length)
                {
                    bestMatch = name;
                }
            }

            return bestMatch ?? RootNamespace(typeName);
        }

        private static string RootNamespace(string typeName)
        {
            var separator = typeName.IndexOf('.');
            return separator < 0 ? typeName : typeName[..separator];
        }

        private static bool IsTypeOfLibrary(string typeName, string libraryName)
        {
            return typeName.Equals(libraryName, StringComparison.OrdinalIgnoreCase)
                || typeName.StartsWith(libraryName + ".", StringComparison.OrdinalIgnoreCase);
        }

        private static string? LookupVersion(IConnectionData connectionData, string library)
        {
            return LibrariesByConnection.TryGetValue(ConnectionKey(connectionData), out var libraries)
                && libraries.TryGetValue(library, out var version)
                ? version
                : null;
        }

        private static string ConnectionKey(IConnectionData connectionData)
        {
            return $"{connectionData.ConnectionType}|{connectionData.Hostname}";
        }

        private static string? ResolveLibraryPath(ICommand command)
        {
            for (int i = command.Payload.Length - 1; i >= 0; i--)
            {
                var path = AsText(command.Payload[i]);
                if (!string.IsNullOrWhiteSpace(path))
                {
                    return path;
                }
            }

            return null;
        }

        private static string? AsText(object? payloadItem)
        {
            if (payloadItem is ICommand { CommandType: CommandType.Value, Payload.Length: > 0 } inner)
            {
                return inner.Payload[0] as string;
            }

            return payloadItem as string;
        }

        private static string ReadVersion(string path)
        {
            if (!File.Exists(path))
            {
                return string.Empty;
            }

            try
            {
                var version = AssemblyName.GetAssemblyName(path).Version;
                if (version != null)
                {
                    return version.Build < 0 ? version.ToString(2) : version.ToString(3);
                }
            }
            catch
            {
                // Not a managed assembly.
            }

            var productVersion = FileVersionInfo.GetVersionInfo(path).ProductVersion;
            return string.IsNullOrWhiteSpace(productVersion) ? string.Empty : productVersion.Trim();
        }
    }
}
