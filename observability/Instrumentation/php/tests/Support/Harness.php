<?php

declare(strict_types=1);

namespace Graftcode\Instrumentation\Tests\Support;

use Graftcode\Instrumentation\CallSpan;
use Graftcode\Instrumentation\GraftcodeInstrumentation;
use Graftcode\Instrumentation\GraftcodeTelemetrySettings;
use Graftcode\Instrumentation\LibraryCatalog;
use HypertubePhpUtils\abstractions\HypertubeCallContext;
use HypertubePhpUtils\Command;
use HypertubePhpUtils\CommandInterface;
use HypertubePhpUtils\connectiondata\IConnectionData;
use HypertubePhpUtils\connectiondata\InMemoryConnectionData;
use HypertubePhpUtils\RuntimeName;
use HypertubePhpUtils\type\CommandType;
use OpenTelemetry\SDK\Logs\Exporter\InMemoryExporter as InMemoryLogExporter;
use OpenTelemetry\SDK\Logs\LoggerProvider;
use OpenTelemetry\SDK\Logs\Processor\SimpleLogRecordProcessor;
use OpenTelemetry\SDK\Logs\ReadableLogRecord;
use OpenTelemetry\SDK\Trace\ImmutableSpan;
use OpenTelemetry\SDK\Trace\Sampler\AlwaysOffSampler;
use OpenTelemetry\SDK\Trace\Sampler\AlwaysOnSampler;
use OpenTelemetry\SDK\Trace\SpanExporter\InMemoryExporter;
use OpenTelemetry\SDK\Trace\SpanProcessor\SimpleSpanProcessor;
use OpenTelemetry\SDK\Trace\TracerProvider;

final class Harness
{
    public GraftcodeInstrumentation $listener;
    public TracerProvider $tracerProvider;
    public LoggerProvider $loggerProvider;

    private InMemoryExporter $spans;
    private InMemoryLogExporter $logs;

    public function __construct(bool $recording = true, ?int $minimumLogLevel = null)
    {
        GraftcodeTelemetrySettings::setAutoInjectEnabled(null);
        GraftcodeTelemetrySettings::setBodyCaptureEnabled(null);
        GraftcodeTelemetrySettings::setBodyMaxCharacters(null);
        LibraryCatalog::reset();

        $this->spans = new InMemoryExporter();
        $this->tracerProvider = TracerProvider::builder()
            ->setSampler($recording ? new AlwaysOnSampler() : new AlwaysOffSampler())
            ->addSpanProcessor(new SimpleSpanProcessor($this->spans))
            ->build();

        $this->logs = new InMemoryLogExporter();
        $this->loggerProvider = LoggerProvider::builder()
            ->addLogRecordProcessor(new SimpleLogRecordProcessor($this->logs))
            ->build();

        $this->listener = GraftcodeInstrumentation::attach($this->tracerProvider, $minimumLogLevel, $this->loggerProvider);
    }

    public function close(): void
    {
        $this->listener->dispose();
        $this->tracerProvider->shutdown();
        $this->loggerProvider->shutdown();
        GraftcodeTelemetrySettings::setAutoInjectEnabled(null);
        GraftcodeTelemetrySettings::setBodyCaptureEnabled(null);
        GraftcodeTelemetrySettings::setBodyMaxCharacters(null);
        LibraryCatalog::reset();
        (new InMemoryConnectionData())->setThreadLocalHeaders([]);
    }

    public function onInvoke(CommandInterface $command, IConnectionData $connectionData): ?CallSpan
    {
        $context = new HypertubeCallContext();
        $this->listener->onExecute($context, $command, $connectionData);

        return $context->items[0] ?? null;
    }

    /**
     * @param array<string, string> $headers
     */
    public function onHandle(CommandInterface $command, array $headers = []): ?CallSpan
    {
        $handled = $command;
        if ($headers !== []) {
            $pairs = [];
            foreach ($headers as $key => $value) {
                $pairs[] = $key;
                $pairs[] = $value;
            }

            $handled = new Command(
                $command->getRuntimeName(),
                CommandType::CONTEXT_METADATA(),
                [new Command($command->getRuntimeName(), CommandType::ARRAY(), $pairs), $command]
            );
        }

        $context = new HypertubeCallContext();
        $this->listener->onProcess($context, $handled);

        return $context->items[0] ?? null;
    }

    /**
     * @return list<ImmutableSpan>
     */
    public function finishedSpans(): array
    {
        return array_values($this->spans->getSpans());
    }

    /**
     * @return list<ReadableLogRecord>
     */
    public function logs(): array
    {
        return array_values($this->logs->getStorage()->getArrayCopy());
    }

    public static function inMemory(): InMemoryConnectionData
    {
        return new InMemoryConnectionData();
    }

    /**
     * @param mixed $value
     */
    public static function value($value): Command
    {
        return new Command(RuntimeName::NETCORE(), CommandType::VALUE(), [$value]);
    }

    /**
     * @param mixed ...$payload
     */
    public static function of(CommandType $type, ...$payload): Command
    {
        return new Command(RuntimeName::NETCORE(), $type, $payload);
    }

    public static function fetch(): Command
    {
        return self::of(
            CommandType::INVOKE_STATIC_METHOD(),
            self::of(CommandType::GET_TYPE(), self::value('SomeLib.Service')),
            self::value('Fetch')
        );
    }
}
