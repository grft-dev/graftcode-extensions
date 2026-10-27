package com.graftcode.instrumentation;

import com.hypertube.utils.CommandType;
import com.hypertube.utils.ICommand;
import com.hypertube.utils.connectiondata.IConnectionData;

import java.io.File;
import java.io.IOException;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import java.util.jar.Attributes;
import java.util.jar.JarFile;
import java.util.jar.Manifest;

final class LibraryCatalog {
    private static final Map<String, Map<String, Library>> librariesByConnection = new ConcurrentHashMap<>();
    private static final Map<String, String> versionByPath = new ConcurrentHashMap<>();

    private LibraryCatalog() {
    }

    private static final class Library {
        final String name;
        final String version;

        Library(String name, String version) {
            this.name = name;
            this.version = version;
        }
    }

    static void record(ICommand command, IConnectionData connectionData) {
        for (ICommand load : findCommands(command, CommandType.LOAD_LIBRARY)) {
            recordLoad(load, connectionData);
        }
    }

    static String describeTarget(ICommand command, IConnectionData connectionData) {
        String channel = channel(connectionData);
        String library = resolveLibraryName(command, connectionData);

        if (OperationName.isBlank(library)) {
            return command.getRuntimeName() + " " + channel;
        }

        String version = lookupVersion(connectionData, library);
        return OperationName.isBlank(version)
                ? library + " " + channel
                : library + " " + version + " " + channel;
    }

    static String channel(IConnectionData connectionData) {
        return connectionData.getConnectionType().name().replace("_", "").toLowerCase(Locale.ROOT);
    }

    private static void recordLoad(ICommand command, IConnectionData connectionData) {
        String path = resolveLibraryPath(command);
        if (OperationName.isBlank(path)) {
            return;
        }

        String name = fileNameWithoutExtension(path);
        if (OperationName.isBlank(name)) {
            return;
        }

        Map<String, Library> libraries = librariesByConnection.computeIfAbsent(
                connectionKey(connectionData),
                key -> new ConcurrentHashMap<>());

        String version = versionByPath.computeIfAbsent(path, LibraryCatalog::readVersion);
        libraries.put(name.toLowerCase(Locale.ROOT), new Library(name, version));
    }

    private static List<ICommand> findCommands(ICommand command, CommandType commandType) {
        List<ICommand> matches = new ArrayList<>();
        collect(command, commandType, matches);
        return matches;
    }

    private static void collect(ICommand command, CommandType commandType, List<ICommand> matches) {
        if (command.getCommandType() == commandType) {
            matches.add(command);
        }

        for (Object item : command.getPayload()) {
            if (item instanceof ICommand) {
                collect((ICommand) item, commandType, matches);
            }
        }
    }

    private static String resolveLibraryName(ICommand command, IConnectionData connectionData) {
        for (ICommand load : findCommands(command, CommandType.LOAD_LIBRARY)) {
            String path = resolveLibraryPath(load);
            String name = OperationName.isBlank(path) ? null : fileNameWithoutExtension(path);
            if (!OperationName.isBlank(name)) {
                return name;
            }
        }

        String typeName = OperationName.resolveTargetTypeName(command);
        if (OperationName.isBlank(typeName)) {
            return null;
        }

        Map<String, Library> libraries = librariesByConnection.get(connectionKey(connectionData));
        if (libraries == null) {
            return rootNamespace(typeName);
        }

        String bestMatch = null;
        for (Library library : libraries.values()) {
            if (!isTypeOfLibrary(typeName, library.name)) {
                continue;
            }

            if (bestMatch == null || library.name.length() > bestMatch.length()) {
                bestMatch = library.name;
            }
        }

        return bestMatch != null ? bestMatch : rootNamespace(typeName);
    }

    private static String rootNamespace(String typeName) {
        int separator = typeName.indexOf('.');
        return separator < 0 ? typeName : typeName.substring(0, separator);
    }

    private static boolean isTypeOfLibrary(String typeName, String libraryName) {
        String type = typeName.toLowerCase(Locale.ROOT);
        String library = libraryName.toLowerCase(Locale.ROOT);
        return type.equals(library) || type.startsWith(library + ".");
    }

    private static String lookupVersion(IConnectionData connectionData, String library) {
        Map<String, Library> libraries = librariesByConnection.get(connectionKey(connectionData));
        if (libraries == null) {
            return null;
        }

        Library entry = libraries.get(library.toLowerCase(Locale.ROOT));
        return entry == null ? null : entry.version;
    }

    private static String connectionKey(IConnectionData connectionData) {
        return connectionData.getConnectionType() + "|" + connectionData.getHostname();
    }

    private static String resolveLibraryPath(ICommand command) {
        Object[] payload = command.getPayload();
        for (int i = payload.length - 1; i >= 0; i--) {
            String path = asText(payload[i]);
            if (!OperationName.isBlank(path)) {
                return path;
            }
        }

        return null;
    }

    private static String asText(Object payloadItem) {
        if (payloadItem instanceof ICommand) {
            ICommand inner = (ICommand) payloadItem;
            if (inner.getCommandType() == CommandType.VALUE
                    && inner.getPayload().length > 0
                    && inner.getPayload()[0] instanceof String) {
                return (String) inner.getPayload()[0];
            }

            return null;
        }

        return payloadItem instanceof String ? (String) payloadItem : null;
    }

    private static String fileNameWithoutExtension(String path) {
        String name = new File(path).getName();
        int dot = name.lastIndexOf('.');
        return dot > 0 ? name.substring(0, dot) : name;
    }

    private static String readVersion(String path) {
        File file = new File(path);
        if (!file.isFile()) {
            return "";
        }

        try (JarFile jar = new JarFile(file)) {
            Manifest manifest = jar.getManifest();
            if (manifest == null) {
                return "";
            }

            Attributes attributes = manifest.getMainAttributes();
            String version = attributes.getValue(Attributes.Name.IMPLEMENTATION_VERSION);
            if (OperationName.isBlank(version)) {
                version = attributes.getValue("Bundle-Version");
            }

            return OperationName.isBlank(version) ? "" : version.trim();
        } catch (IOException | SecurityException ex) {
            return "";
        }
    }
}
