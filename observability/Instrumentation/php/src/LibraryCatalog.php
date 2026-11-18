<?php

declare(strict_types=1);

namespace Graftcode\Instrumentation;

use HypertubePhpUtils\CommandInterface;
use HypertubePhpUtils\connectiondata\IConnectionData;
use HypertubePhpUtils\logging\LogsCommandResolver;
use HypertubePhpUtils\type\CommandType;
use HypertubePhpUtils\type\ConnectionType;

final class LibraryCatalog
{
    /** @var array<string, array<string, array{name: string, version: string}>> */
    private static array $librariesByConnection = [];

    /** @var array<string, string> */
    private static array $versionByPath = [];

    private function __construct()
    {
    }

    public static function reset(): void
    {
        self::$librariesByConnection = [];
        self::$versionByPath = [];
    }

    public static function record(CommandInterface $command, IConnectionData $connectionData): void
    {
        foreach (self::findCommands($command, CommandType::LOAD_LIBRARY) as $load) {
            self::recordLoad($load, $connectionData);
        }
    }

    public static function describeTarget(CommandInterface $command, IConnectionData $connectionData): string
    {
        $channel = strtolower(self::getConnectionTypeName($connectionData->getConnectionType()));
        $library = self::resolveLibraryName($command, $connectionData);

        if (OperationName::isBlank($library)) {
            return LogsCommandResolver::getRuntimeName($command->getRuntimeName()) . ' ' . $channel;
        }

        $version = self::lookupVersion($connectionData, $library);

        return OperationName::isBlank($version)
            ? $library . ' ' . $channel
            : $library . ' ' . $version . ' ' . $channel;
    }

    /**
     * Matches the .NET enum names, e.g. WEB_SOCKET is reported as WebSocket.
     */
    public static function getConnectionTypeName(ConnectionType $connectionType): string
    {
        return str_replace('_', '', ucwords(strtolower($connectionType->getName()), '_'));
    }

    private static function recordLoad(CommandInterface $command, IConnectionData $connectionData): void
    {
        $path = self::resolveLibraryPath($command);
        if (OperationName::isBlank($path)) {
            return;
        }

        $name = self::fileNameWithoutExtension($path);
        if (OperationName::isBlank($name)) {
            return;
        }

        if (!array_key_exists($path, self::$versionByPath)) {
            self::$versionByPath[$path] = self::readVersion($path);
        }

        self::$librariesByConnection[self::connectionKey($connectionData)][strtolower($name)] = [
            'name' => $name,
            'version' => self::$versionByPath[$path],
        ];
    }

    /**
     * @return list<CommandInterface>
     */
    private static function findCommands(CommandInterface $command, int $commandType): array
    {
        $matches = [];
        if ($command->getCommandType()->equalsByValue($commandType)) {
            $matches[] = $command;
        }

        foreach ($command->getPayload() as $item) {
            if ($item instanceof CommandInterface) {
                array_push($matches, ...self::findCommands($item, $commandType));
            }
        }

        return $matches;
    }

    private static function resolveLibraryName(CommandInterface $command, IConnectionData $connectionData): ?string
    {
        foreach (self::findCommands($command, CommandType::LOAD_LIBRARY) as $load) {
            $path = self::resolveLibraryPath($load);
            $name = OperationName::isBlank($path) ? null : self::fileNameWithoutExtension($path);
            if (!OperationName::isBlank($name)) {
                return $name;
            }
        }

        $typeName = OperationName::resolveTargetTypeName($command);
        if (OperationName::isBlank($typeName)) {
            return null;
        }

        $libraries = self::$librariesByConnection[self::connectionKey($connectionData)] ?? null;
        if ($libraries === null) {
            return self::rootNamespace($typeName);
        }

        $bestMatch = null;
        foreach ($libraries as $library) {
            if (!self::isTypeOfLibrary($typeName, $library['name'])) {
                continue;
            }

            if ($bestMatch === null || strlen($library['name']) > strlen($bestMatch)) {
                $bestMatch = $library['name'];
            }
        }

        return $bestMatch ?? self::rootNamespace($typeName);
    }

    private static function rootNamespace(string $typeName): string
    {
        $separator = strpos($typeName, '.');

        return $separator === false ? $typeName : substr($typeName, 0, $separator);
    }

    private static function isTypeOfLibrary(string $typeName, string $libraryName): bool
    {
        return strcasecmp($typeName, $libraryName) === 0
            || stripos($typeName, $libraryName . '.') === 0;
    }

    private static function lookupVersion(IConnectionData $connectionData, string $library): ?string
    {
        return self::$librariesByConnection[self::connectionKey($connectionData)][strtolower($library)]['version'] ?? null;
    }

    private static function connectionKey(IConnectionData $connectionData): string
    {
        return $connectionData->getConnectionType()->getValue() . '|' . $connectionData->getHostname();
    }

    private static function resolveLibraryPath(CommandInterface $command): ?string
    {
        $payload = $command->getPayload();
        for ($i = count($payload) - 1; $i >= 0; $i--) {
            $path = self::asText($payload[$i] ?? null);
            if (!OperationName::isBlank($path)) {
                return $path;
            }
        }

        return null;
    }

    /**
     * @param mixed $payloadItem
     */
    private static function asText($payloadItem): ?string
    {
        if ($payloadItem instanceof CommandInterface
            && $payloadItem->getCommandType()->equalsByValue(CommandType::VALUE)
            && is_string($payloadItem->getPayload()[0] ?? null)) {
            return $payloadItem->getPayload()[0];
        }

        return is_string($payloadItem) ? $payloadItem : null;
    }

    private static function fileNameWithoutExtension(string $path): string
    {
        $name = basename(str_replace('\\', '/', rtrim($path, '/\\')));
        $dot = strrpos($name, '.');

        return $dot === false || $dot === 0 ? $name : substr($name, 0, $dot);
    }

    /**
     * A PHP library is a folder or file next to a composer.json that carries its version.
     */
    private static function readVersion(string $path): string
    {
        $directory = is_dir($path) ? $path : dirname($path);
        $composerJson = $directory . DIRECTORY_SEPARATOR . 'composer.json';
        if (!is_file($composerJson)) {
            return '';
        }

        $manifest = json_decode((string) @file_get_contents($composerJson), true);

        return is_array($manifest) && is_string($manifest['version'] ?? null) ? trim($manifest['version']) : '';
    }
}
