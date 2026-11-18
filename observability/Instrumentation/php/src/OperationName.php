<?php

declare(strict_types=1);

namespace Graftcode\Instrumentation;

use HypertubePhpUtils\CommandInterface;
use HypertubePhpUtils\logging\LogsCommandResolver;
use HypertubePhpUtils\type\CommandType;

final class OperationName
{
    private const MAX_DEPTH = 32;

    private const MEMBER_COMMANDS = [
        CommandType::INVOKE_STATIC_METHOD,
        CommandType::INVOKE_INSTANCE_METHOD,
        CommandType::INVOKE_GENERIC_STATIC_METHOD,
        CommandType::INVOKE_GENERIC_METHOD,
        CommandType::GET_STATIC_FIELD,
        CommandType::SET_STATIC_FIELD,
        CommandType::GET_INSTANCE_FIELD,
        CommandType::SET_INSTANCE_FIELD,
        CommandType::GET_STATIC_METHOD_AS_DELEGATE,
        CommandType::GET_INSTANCE_METHOD_AS_DELEGATE,
    ];

    private const TYPE_FROM_TARGET_COMMANDS = [
        CommandType::VALUE,
        CommandType::CREATE_INSTANCE,
        CommandType::SET_INSTANCE_FIELD,
        CommandType::INVOKE_STATIC_METHOD,
        CommandType::INVOKE_INSTANCE_METHOD,
        CommandType::GET_STATIC_FIELD,
        CommandType::GET_INSTANCE_FIELD,
    ];

    private const TYPE_FROM_TEXT_COMMANDS = [
        CommandType::GET_TYPE,
        CommandType::CAST,
        CommandType::CREATE_NULL,
    ];

    private function __construct()
    {
    }

    public static function resolve(CommandInterface $command): string
    {
        $type = $command->getCommandType()->getValue();

        if (in_array($type, self::MEMBER_COMMANDS, true)) {
            return self::qualify(
                self::resolveTypeNameCore(self::payloadAt($command, 0), 0),
                self::resolveText(self::payloadAt($command, 1), 0)
            ) ?? self::fallback($command);
        }

        if ($type === CommandType::CREATE_INSTANCE) {
            $createdType = self::resolveTypeNameCore(self::payloadAt($command, 0), 0);

            return $createdType !== null ? $createdType . '.ctor' : self::fallback($command);
        }

        if ($type === CommandType::INVOKE_GLOBAL_FUNCTION || $type === CommandType::GET_GLOBAL_FIELD) {
            return self::resolveText(self::payloadAt($command, 0), 0) ?? self::fallback($command);
        }

        return self::fallback($command);
    }

    public static function resolveTargetTypeName(CommandInterface $command): ?string
    {
        $type = $command->getCommandType()->getValue();

        if (in_array($type, self::MEMBER_COMMANDS, true) || $type === CommandType::CREATE_INSTANCE) {
            return self::resolveTypeNameCore(self::payloadAt($command, 0), 0);
        }

        if ($type === CommandType::GET_TYPE) {
            return self::resolveTypeNameCore($command, 0);
        }

        return null;
    }

    /**
     * @param mixed $value
     */
    public static function isBlank($value): bool
    {
        return $value === null || trim((string) $value) === '';
    }

    private static function qualify(?string $typeName, ?string $memberName): ?string
    {
        if (self::isBlank($memberName)) {
            return null;
        }

        return self::isBlank($typeName) ? $memberName : $typeName . '.' . $memberName;
    }

    private static function fallback(CommandInterface $command): string
    {
        return 'hypertube.' . LogsCommandResolver::getCommandTypeName($command->getCommandType());
    }

    /**
     * @return mixed
     */
    private static function payloadAt(CommandInterface $command, int $index)
    {
        return $command->getPayload()[$index] ?? null;
    }

    /**
     * @param mixed $payloadItem
     */
    private static function resolveText($payloadItem, int $depth): ?string
    {
        if ($depth > self::MAX_DEPTH) {
            return null;
        }

        if (is_string($payloadItem)) {
            return trim($payloadItem) === '' ? null : trim($payloadItem);
        }

        if (!$payloadItem instanceof CommandInterface
            || !$payloadItem->getCommandType()->equalsByValue(CommandType::VALUE)) {
            return null;
        }

        return self::resolveText(self::payloadAt($payloadItem, 0), $depth + 1);
    }

    /**
     * @param mixed $target
     */
    private static function resolveTypeNameCore($target, int $depth): ?string
    {
        if ($depth > self::MAX_DEPTH || $target === null) {
            return null;
        }

        if (is_string($target)) {
            return trim($target) === '' ? null : self::normalizeTypeName($target);
        }

        if ($target instanceof CommandInterface) {
            return self::resolveTypeNameFromCommand($target, $depth);
        }

        return is_object($target) ? self::normalizeTypeName(get_class($target)) : gettype($target);
    }

    private static function resolveTypeNameFromCommand(CommandInterface $command, int $depth): ?string
    {
        $type = $command->getCommandType()->getValue();

        if (in_array($type, self::TYPE_FROM_TARGET_COMMANDS, true)) {
            return self::resolveTypeNameCore(self::payloadAt($command, 0), $depth + 1);
        }

        if (in_array($type, self::TYPE_FROM_TEXT_COMMANDS, true)) {
            foreach ($command->getPayload() as $item) {
                $name = self::resolveText($item, $depth + 1);
                if (!self::isBlank($name)) {
                    return self::normalizeTypeName($name);
                }
            }
        }

        return null;
    }

    /**
     * PHP type names use backslash namespaces; spans use the dotted form gg reports for PHP types.
     */
    private static function normalizeTypeName(string $typeName): string
    {
        return str_replace('\\', '.', ltrim(trim($typeName), '\\'));
    }
}
