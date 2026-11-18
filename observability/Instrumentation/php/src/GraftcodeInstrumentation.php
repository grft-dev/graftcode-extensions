<?php

declare(strict_types=1);

namespace Graftcode\Instrumentation;

use HypertubePhpUtils\abstractions\HypertubeBroker;
use HypertubePhpUtils\abstractions\HypertubeCallContext;
use HypertubePhpUtils\abstractions\IHypertubeHandling;
use HypertubePhpUtils\abstractions\IHypertubeInvocation;
use HypertubePhpUtils\abstractions\IHypertubeLogging;
use HypertubePhpUtils\CommandInterface;
use HypertubePhpUtils\connectiondata\IConnectionData;
use HypertubePhpUtils\logging\HypertubeLogger;
use HypertubePhpUtils\logging\LogsCommandResolver;
use HypertubePhpUtils\logging\LogsLevel;
use HypertubePhpUtils\logging\LogsRedactor;
use OpenTelemetry\API\Logs\LoggerInterface;
use OpenTelemetry\API\Logs\LoggerProviderInterface;
use OpenTelemetry\API\Logs\LogRecord;
use OpenTelemetry\API\Trace\Span;
use OpenTelemetry\API\Trace\TracerInterface;
use OpenTelemetry\API\Trace\TracerProviderInterface;
use OpenTelemetry\Context\Context;
use Throwable;

final class GraftcodeInstrumentation implements IHypertubeInvocation, IHypertubeHandling, IHypertubeLogging
{
    public const INSTRUMENTATION_NAME = 'Graftcode';
    public const LOGGER_NAME = 'Hypertube';
    public const VERSION = '1.0.0';

    private const SEVERITY_NUMBER = [
        LogsLevel::TRACE => 1,
        LogsLevel::DEBUG => 5,
        LogsLevel::INFO => 9,
        LogsLevel::WARNING => 13,
        LogsLevel::ERROR => 17,
    ];

    private TracerInterface $tracer;
    private ?int $minimumLogLevel;
    private ?LoggerInterface $logger;

    private function __construct(TracerProviderInterface $tracerProvider, ?int $minimumLogLevel, ?LoggerProviderInterface $loggerProvider)
    {
        $this->tracer = $tracerProvider->getTracer(self::INSTRUMENTATION_NAME, self::VERSION);
        $this->minimumLogLevel = $minimumLogLevel;
        $this->logger = $minimumLogLevel === null || $loggerProvider === null
            ? null
            : $loggerProvider->getLogger(self::LOGGER_NAME, self::VERSION);

        HypertubeBroker::attach($this);
        HypertubeLogger::attach($this);
    }

    /**
     * Without a logger provider, Hypertube logs are recorded as events on the current span.
     */
    public static function attach(
        TracerProviderInterface $tracerProvider,
        ?int $minimumLogLevel = null,
        ?LoggerProviderInterface $loggerProvider = null
    ): GraftcodeInstrumentation {
        return new GraftcodeInstrumentation($tracerProvider, $minimumLogLevel, $loggerProvider);
    }

    public function dispose(): void
    {
        HypertubeBroker::detach($this);
        HypertubeLogger::detach($this);
    }

    public function onExecute(HypertubeCallContext $context, CommandInterface $command, IConnectionData $connectionData): void
    {
        $span = CallSpan::startInvoke($this->tracer, LogsCommandResolver::resolveForInvoke($command), $connectionData);
        if ($span !== null) {
            $context->items[] = $span;
        }
    }

    public function onResponse(HypertubeCallContext $context, CommandInterface $response): void
    {
        CallSpan::completeSpans($context, $response, null);
    }

    public function onInvocationError(HypertubeCallContext $context, Throwable $exception): void
    {
        CallSpan::completeSpans($context, null, $exception);
    }

    public function onProcess(HypertubeCallContext $context, CommandInterface $command): void
    {
        $headers = [];
        $reported = LogsCommandResolver::resolveForHandle($command, $headers);
        $span = CallSpan::startHandle($this->tracer, $reported, $headers);
        if ($span !== null) {
            $context->items[] = $span;
        }
    }

    public function onProcessed(HypertubeCallContext $context, CommandInterface $response): void
    {
        CallSpan::completeSpans($context, $response, null);
    }

    public function onHandlingError(HypertubeCallContext $context, Throwable $exception): void
    {
        CallSpan::completeSpans($context, null, $exception);
    }

    public function isRequested(): bool
    {
        if ($this->minimumLogLevel === null) {
            return Span::getCurrent()->isRecording();
        }

        return $this->minimumLogLevel !== LogsLevel::OFF;
    }

    public function isLogEnabled(int $level): bool
    {
        if ($this->minimumLogLevel === null) {
            return Span::getCurrent()->isRecording();
        }

        if ($level === LogsLevel::OFF || $this->minimumLogLevel === LogsLevel::OFF) {
            return false;
        }

        return LogsLevel::isAtLeast($level, $this->minimumLogLevel);
    }

    public function onLog(int $level, string $message, array $properties, ?Throwable $exception): void
    {
        if (!$this->isLogEnabled($level)) {
            return;
        }

        if ($this->logger === null) {
            self::recordSpanEvent($level, $message, $properties, $exception);

            return;
        }

        $attributes = $properties;
        $attributes['{OriginalFormat}'] = $message;
        self::mergeExceptionAttributes($attributes, $exception);

        $this->logger->emit(
            (new LogRecord($message))
                ->setTimestamp((int) (microtime(true) * 1e9))
                ->setSeverityNumber(self::SEVERITY_NUMBER[$level] ?? 0)
                ->setSeverityText(self::severityText($level))
                ->setAttributes($attributes)
                ->setContext(Context::getCurrent())
        );
    }

    /**
     * @param array<string, string> $properties
     */
    private static function recordSpanEvent(int $level, string $message, array $properties, ?Throwable $exception): void
    {
        $span = Span::getCurrent();
        if (!$span->isRecording()) {
            return;
        }

        $attributes = ['log.level' => self::severityText($level)];
        foreach ($properties as $key => $value) {
            $attributes[$key] = $value;
        }

        if ($exception === null) {
            $span->addEvent($message, $attributes);

            return;
        }

        $attributes['log.message'] = $message;
        self::mergeExceptionAttributes($attributes, $exception);
        $span->addEvent('exception', $attributes);
    }

    /**
     * @param array<string, mixed> $attributes
     */
    private static function mergeExceptionAttributes(array &$attributes, ?Throwable $exception): void
    {
        if ($exception === null) {
            return;
        }

        $attributes['exception.type'] = get_class($exception);
        $attributes['exception.message'] = LogsRedactor::redactValue($exception->getMessage());
        $attributes['exception.stacktrace'] = LogsRedactor::redactValue((string) $exception);
    }

    private static function severityText(int $level): string
    {
        return $level === LogsLevel::INFO ? 'Information' : LogsLevel::getName($level);
    }
}
