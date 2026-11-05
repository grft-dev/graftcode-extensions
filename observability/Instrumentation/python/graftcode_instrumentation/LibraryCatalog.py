import os
import threading
import zipfile

from hypertube.utils.Command import Command
from hypertube.utils.CommandType import CommandType

from graftcode_instrumentation.OperationName import OperationName


class _Library:
    __slots__ = ("name", "version")

    def __init__(self, name, version):
        self.name = name
        self.version = version


class LibraryCatalog:
    _libraries_by_connection = {}
    _version_by_path = {}
    _lock = threading.Lock()

    @staticmethod
    def record(command, connection_data):
        for load in LibraryCatalog._find_commands(command, CommandType.LoadLibrary):
            LibraryCatalog._record_load(load, connection_data)

    @staticmethod
    def describe_target(command, connection_data):
        channel = LibraryCatalog.channel(connection_data)
        library = LibraryCatalog._resolve_library_name(command, connection_data)

        if OperationName.is_blank(library):
            return str(command.runtime_name) + " " + channel

        version = LibraryCatalog._lookup_version(connection_data, library)
        if OperationName.is_blank(version):
            return library + " " + channel
        return library + " " + version + " " + channel

    @staticmethod
    def channel(connection_data):
        return connection_data.connection_type.name.replace("_", "").lower()

    @staticmethod
    def _record_load(command, connection_data):
        path = LibraryCatalog._resolve_library_path(command)
        if OperationName.is_blank(path):
            return

        name = LibraryCatalog._file_name_without_extension(path)
        if OperationName.is_blank(name):
            return

        with LibraryCatalog._lock:
            version = LibraryCatalog._version_by_path.get(path)
            if version is None:
                version = LibraryCatalog._read_version(path, name)
                LibraryCatalog._version_by_path[path] = version

            libraries = LibraryCatalog._libraries_by_connection.setdefault(
                LibraryCatalog._connection_key(connection_data), {})
            libraries[name.lower()] = _Library(name, version)

    @staticmethod
    def _find_commands(command, command_type):
        matches = []
        LibraryCatalog._collect(command, command_type, matches)
        return matches

    @staticmethod
    def _collect(command, command_type, matches):
        if command.command_type == command_type:
            matches.append(command)

        for item in command.payload:
            if isinstance(item, Command):
                LibraryCatalog._collect(item, command_type, matches)

    @staticmethod
    def _resolve_library_name(command, connection_data):
        for load in LibraryCatalog._find_commands(command, CommandType.LoadLibrary):
            path = LibraryCatalog._resolve_library_path(load)
            name = None if OperationName.is_blank(path) else LibraryCatalog._file_name_without_extension(path)
            if not OperationName.is_blank(name):
                return name

        type_name = OperationName.resolve_target_type_name(command)
        if OperationName.is_blank(type_name):
            return None

        libraries = LibraryCatalog._libraries_by_connection.get(LibraryCatalog._connection_key(connection_data))
        if not libraries:
            return LibraryCatalog._root_namespace(type_name)

        best_match = None
        for library in list(libraries.values()):
            if not LibraryCatalog._is_type_of_library(type_name, library.name):
                continue

            if best_match is None or len(library.name) > len(best_match):
                best_match = library.name

        return best_match if best_match is not None else LibraryCatalog._root_namespace(type_name)

    @staticmethod
    def _root_namespace(type_name):
        separator = type_name.find(".")
        return type_name if separator < 0 else type_name[:separator]

    @staticmethod
    def _is_type_of_library(type_name, library_name):
        type_lower = type_name.lower()
        library_lower = library_name.lower()
        return type_lower == library_lower or type_lower.startswith(library_lower + ".")

    @staticmethod
    def _lookup_version(connection_data, library):
        libraries = LibraryCatalog._libraries_by_connection.get(LibraryCatalog._connection_key(connection_data))
        if not libraries:
            return None

        entry = libraries.get(library.lower())
        return None if entry is None else entry.version

    @staticmethod
    def _connection_key(connection_data):
        return connection_data.connection_type.name + "|" + str(connection_data.hostname)

    @staticmethod
    def _resolve_library_path(command):
        payload = command.payload
        for i in range(len(payload) - 1, -1, -1):
            path = LibraryCatalog._as_text(payload[i])
            if not OperationName.is_blank(path):
                return path

        return None

    @staticmethod
    def _as_text(payload_item):
        if isinstance(payload_item, Command):
            if (payload_item.command_type == CommandType.Value
                    and payload_item.payload
                    and isinstance(payload_item.payload[0], str)):
                return payload_item.payload[0]
            return None

        return payload_item if isinstance(payload_item, str) else None

    @staticmethod
    def _file_name_without_extension(path):
        name = os.path.basename(path.rstrip("/\\"))
        dot = name.rfind(".")
        return name[:dot] if dot > 0 else name

    @staticmethod
    def _read_version(path, name):
        if path.lower().endswith(".jar") and os.path.isfile(path):
            return LibraryCatalog._read_jar_version(path)

        if os.path.isdir(path) or path.lower().endswith(".py"):
            return LibraryCatalog._read_distribution_version(name)

        return ""

    @staticmethod
    def _read_jar_version(path):
        try:
            with zipfile.ZipFile(path) as jar:
                manifest = jar.read("META-INF/MANIFEST.MF").decode("utf-8", errors="replace")
        except (OSError, KeyError, zipfile.BadZipFile):
            return ""

        attributes = {}
        for line in manifest.splitlines():
            key, separator, value = line.partition(":")
            if separator:
                attributes[key.strip()] = value.strip()

        version = attributes.get("Implementation-Version") or attributes.get("Bundle-Version")
        return "" if OperationName.is_blank(version) else version.strip()

    @staticmethod
    def _read_distribution_version(name):
        try:
            from importlib import metadata
            return metadata.version(name).strip()
        except Exception:
            return ""
