<?php

declare(strict_types=1);

namespace Graftcode\Telemetry\Datadog;

use HypertubePhpUtils\logging\LogsLevel;
use InvalidArgumentException;

final class GraftCodeDatadogConnectorOptions
{
    public const ENDPOINT_VARIABLE = 'OTEL_EXPORTER_OTLP_ENDPOINT';
    public const ENVIRONMENT_VARIABLE = 'DD_ENV';
    public const VERSION_VARIABLE = 'DD_VERSION';
    public const DEFAULT_ENDPOINT = 'http://127.0.0.1:4318';
    public const HOSTED_RECEIVER = '/Binaries/Php/hypertube/core/receiver/Receiver.php';

    public string $serviceName = '';
    public ?string $endpoint = null;
    public ?string $environment = null;
    public ?string $version = null;
    public bool $enableConsoleLogging = true;
    public ?bool $flushAfterCalls = null;

    private ?int $minimumLogLevel = LogsLevel::INFO;

    /** @var array<string, string>|null */
    private ?array $environmentVariables;

    /**
     * @param array<string, string>|null $environmentVariables defaults to the process environment
     */
    public function __construct(?array $environmentVariables = null)
    {
        $this->environmentVariables = $environmentVariables;
    }

    public function getMinimumLogLevel(): ?int
    {
        return $this->minimumLogLevel;
    }

    /**
     * @param int|string $level a LogsLevel constant or its name, e.g. "debug"
     */
    public function setMinimumLogLevel($level): void
    {
        $this->minimumLogLevel = is_string($level) ? LogsLevel::parse($level) : $level;
    }

    /**
     * A runtime hosted by the Hypertube native launcher loads its receiver from the extracted Binaries folder.
     *
     * @param list<string>|null $includedFiles defaults to get_included_files()
     */
    public function resolveFlushAfterCalls(?array $includedFiles = null): bool
    {
        if ($this->flushAfterCalls !== null) {
            return $this->flushAfterCalls;
        }

        foreach ($includedFiles ?? get_included_files() as $path) {
            if (str_ends_with(str_replace('\\', '/', $path), self::HOSTED_RECEIVER)) {
                return true;
            }
        }

        return false;
    }

    public function resolveEndpoint(): string
    {
        return rtrim(self::present($this->endpoint) ?? $this->variable(self::ENDPOINT_VARIABLE) ?? self::DEFAULT_ENDPOINT, '/');
    }

    public function resolveEnvironment(): ?string
    {
        return self::present($this->environment) ?? $this->variable(self::ENVIRONMENT_VARIABLE);
    }

    public function resolveVersion(): ?string
    {
        return self::present($this->version) ?? $this->variable(self::VERSION_VARIABLE);
    }

    public function validate(): void
    {
        if (self::present($this->serviceName) === null) {
            throw new InvalidArgumentException('serviceName must be configured for Datadog telemetry.');
        }

        if ($this->minimumLogLevel === null) {
            throw new InvalidArgumentException('minimumLogLevel must be a Hypertube LogsLevel.');
        }
    }

    private function variable(string $name): ?string
    {
        if ($this->environmentVariables !== null) {
            return self::present($this->environmentVariables[$name] ?? null);
        }

        $value = getenv($name);

        return self::present($value === false ? null : $value);
    }

    private static function present(?string $value): ?string
    {
        $text = $value === null ? null : trim($value);

        return $text === null || $text === '' ? null : $text;
    }
}
