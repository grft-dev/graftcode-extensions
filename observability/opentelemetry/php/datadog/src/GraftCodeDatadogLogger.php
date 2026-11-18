<?php

declare(strict_types=1);

namespace Graftcode\Telemetry\Datadog;

use HypertubePhpUtils\logging\LogsLevel;
use OpenTelemetry\API\Logs\LoggerInterface;
use OpenTelemetry\API\Logs\LogRecord;
use OpenTelemetry\API\Logs\Severity;
use OpenTelemetry\Context\Context;
use Throwable;


final class GraftCodeDatadogLogger
{
    public const CONSOLE_INDENT = '      ';
    public const ORIGINAL_FORMAT = '{OriginalFormat}';

    private const SEVERITY = [
        LogsLevel::TRACE => [Severity::TRACE, 'trace', 'trce'],
        LogsLevel::DEBUG => [Severity::DEBUG, 'debug', 'dbug'],
        LogsLevel::INFO => [Severity::INFO, 'info', 'info'],
        LogsLevel::WARNING => [Severity::WARN, 'warn', 'warn'],
        LogsLevel::ERROR => [Severity::ERROR, 'error', 'fail'],
    ];

    private string $category;
    private LoggerInterface $otelLogger;
    private int $minimumLevel;
    /** @var resource|null */
    private $console;

    /**
     * @param resource|null $console
     */
    public function __construct(string $category, LoggerInterface $otelLogger, int $minimumLevel, $console)
    {
        $this->category = $category;
        $this->otelLogger = $otelLogger;
        $this->minimumLevel = $minimumLevel;
        $this->console = $console;
    }

    public function getCategory(): string
    {
        return $this->category;
    }

    public function getMinimumLevel(): int
    {
        return $this->minimumLevel;
    }

    public function isEnabled(int $level): bool
    {
        return $level !== LogsLevel::OFF
            && $this->minimumLevel !== LogsLevel::OFF
            && LogsLevel::isAtLeast($level, $this->minimumLevel);
    }

    /** @param mixed ...$args */
    public function trace(string $template, ...$args): void
    {
        $this->logArguments(LogsLevel::TRACE, $template, $args);
    }

    /** @param mixed ...$args */
    public function debug(string $template, ...$args): void
    {
        $this->logArguments(LogsLevel::DEBUG, $template, $args);
    }

    /** @param mixed ...$args */
    public function info(string $template, ...$args): void
    {
        $this->logArguments(LogsLevel::INFO, $template, $args);
    }

    /** @param mixed ...$args */
    public function warning(string $template, ...$args): void
    {
        $this->logArguments(LogsLevel::WARNING, $template, $args);
    }

    /** @param mixed ...$args */
    public function warn(string $template, ...$args): void
    {
        $this->logArguments(LogsLevel::WARNING, $template, $args);
    }

    /** @param mixed ...$args */
    public function error(string $template, ...$args): void
    {
        $this->logArguments(LogsLevel::ERROR, $template, $args);
    }

    /**
     * @param list<mixed> $args
     */
    public function log(int $level, string $template, array $args = [], ?Throwable $exception = null): void
    {
        if (!$this->isEnabled($level)) {
            return;
        }

        $properties = [];
        $message = MessageTemplate::render($template, $args, $properties);

        $this->emit($level, $template, $message, $properties, $exception);
        $this->writeToConsole($level, $message, $exception);
    }

    /**
     * @param array<int|string, mixed> $args
     */
    private function logArguments(int $level, string $template, array $args): void
    {
        $exception = $args['exception'] ?? null;
        unset($args['exception']);

        $this->log($level, $template, array_values($args), $exception instanceof Throwable ? $exception : null);
    }

    /**
     * @param array<string, string> $properties
     */
    private function emit(int $level, string $template, string $message, array $properties, ?Throwable $exception): void
    {
        [$severity, $severityText] = self::SEVERITY[$level];
        $attributes = $properties;
        $attributes[self::ORIGINAL_FORMAT] = $template;
        if ($exception !== null) {
            $attributes['exception.type'] = get_class($exception);
            $attributes['exception.message'] = $exception->getMessage();
            $attributes['exception.stacktrace'] = (string) $exception;
        }

        $this->otelLogger->emit(
            (new LogRecord($message))
                ->setTimestamp((int) (microtime(true) * 1e9))
                ->setSeverityNumber($severity)
                ->setSeverityText($severityText)
                ->setAttributes($attributes)
                ->setContext(Context::getCurrent())
        );
    }

    private function writeToConsole(int $level, string $message, ?Throwable $exception): void
    {
        if ($this->console === null) {
            return;
        }

        $line = self::SEVERITY[$level][2] . ': ' . $this->category . "[0]\n" . self::CONSOLE_INDENT . $message;
        if ($exception !== null) {
            $line .= "\n" . trim((string) $exception);
        }

        fwrite($this->console, $line . "\n");
    }
}
