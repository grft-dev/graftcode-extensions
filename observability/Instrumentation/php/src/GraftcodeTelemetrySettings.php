<?php

declare(strict_types=1);

namespace Graftcode\Instrumentation;

final class GraftcodeTelemetrySettings
{
    public const MINIMUM_BODY_MAX_CHARS = 256;
    public const DEFAULT_BODY_MAX_CHARS = 8192;
    public const MAXIMUM_BODY_MAX_CHARS = 32768;

    private static ?bool $autoInject = null;
    private static ?bool $captureBodies = null;
    private static ?int $bodyMaxCharacters = null;

    private function __construct()
    {
    }

    public static function isAutoInjectEnabled(): bool
    {
        if (self::$autoInject === null) {
            self::$autoInject = self::readFlag('HYPERTUBE_TELEMETRY_AUTO_INJECT', true);
        }

        return self::$autoInject;
    }

    public static function setAutoInjectEnabled(?bool $value): void
    {
        self::$autoInject = $value;
    }

    public static function isBodyCaptureEnabled(): bool
    {
        if (self::$captureBodies === null) {
            self::$captureBodies = self::readFlag('HYPERTUBE_TELEMETRY_CAPTURE_BODIES', false);
        }

        return self::$captureBodies;
    }

    public static function setBodyCaptureEnabled(?bool $value): void
    {
        self::$captureBodies = $value;
    }

    public static function getBodyMaxCharacters(): int
    {
        if (self::$bodyMaxCharacters === null) {
            self::$bodyMaxCharacters = self::readBodyMaxCharacters();
        }

        return self::$bodyMaxCharacters;
    }

    public static function setBodyMaxCharacters(?int $value): void
    {
        self::$bodyMaxCharacters = $value === null ? null : self::clampBodyMaxCharacters($value);
    }

    public static function clampBodyMaxCharacters(int $value): int
    {
        return max(self::MINIMUM_BODY_MAX_CHARS, min($value, self::MAXIMUM_BODY_MAX_CHARS));
    }

    private static function readFlag(string $name, bool $defaultValue): bool
    {
        $value = getenv($name);
        if ($value === false || trim($value) === '') {
            return $defaultValue;
        }

        $value = strtolower(trim($value));

        return $value !== 'false' && $value !== '0';
    }

    private static function readBodyMaxCharacters(): int
    {
        $raw = getenv('HYPERTUBE_TELEMETRY_BODY_MAX_CHARS');
        if ($raw === false || preg_match('/^\s*[+-]?\d+\s*$/', $raw) !== 1) {
            return self::DEFAULT_BODY_MAX_CHARS;
        }

        return self::clampBodyMaxCharacters((int) trim($raw));
    }
}
