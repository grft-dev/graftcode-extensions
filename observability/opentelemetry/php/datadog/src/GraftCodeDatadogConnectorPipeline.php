<?php

declare(strict_types=1);

namespace Graftcode\Telemetry\Datadog;

use Closure;
use Graftcode\Instrumentation\GraftcodeInstrumentation;
use HypertubePhpUtils\abstractions\HypertubeBroker;
use OpenTelemetry\API\Baggage\Propagation\BaggagePropagator;
use OpenTelemetry\API\Common\Time\Clock;
use OpenTelemetry\API\Trace\Propagation\TraceContextPropagator;
use OpenTelemetry\API\Trace\SpanInterface;
use OpenTelemetry\API\Trace\SpanKind;
use OpenTelemetry\API\Trace\StatusCode;
use OpenTelemetry\API\Trace\TracerInterface;
use OpenTelemetry\Context\Propagation\MultiTextMapPropagator;
use OpenTelemetry\Context\ScopeInterface;
use OpenTelemetry\Contrib\Otlp\LogsExporter;
use OpenTelemetry\Contrib\Otlp\OtlpHttpTransportFactory;
use OpenTelemetry\Contrib\Otlp\SpanExporter;
use OpenTelemetry\SDK\Common\Attribute\Attributes;
use OpenTelemetry\SDK\Logs\LoggerProvider;
use OpenTelemetry\SDK\Logs\LogRecordExporterInterface;
use OpenTelemetry\SDK\Logs\LogRecordProcessorInterface;
use OpenTelemetry\SDK\Logs\Processor\BatchLogRecordProcessor;
use OpenTelemetry\SDK\Logs\Processor\SimpleLogRecordProcessor;
use OpenTelemetry\SDK\Resource\ResourceInfo;
use OpenTelemetry\SDK\Resource\ResourceInfoFactory;
use OpenTelemetry\SDK\Sdk;
use OpenTelemetry\SDK\Trace\SpanExporterInterface;
use OpenTelemetry\SDK\Trace\SpanProcessor\BatchSpanProcessor;
use OpenTelemetry\SDK\Trace\SpanProcessor\SimpleSpanProcessor;
use OpenTelemetry\SDK\Trace\SpanProcessorInterface;
use OpenTelemetry\SDK\Trace\TracerProvider;
use Throwable;

final class GraftCodeDatadogConnectorPipeline
{
    public const VERSION = '1.0.0';
    public const TRACES_PATH = '/v1/traces';
    public const LOGS_PATH = '/v1/logs';
    private const PROTOBUF = 'application/x-protobuf';

    private string $serviceName;
    private string $endpoint;
    private TracerProvider $tracerProvider;
    private LoggerProvider $loggerProvider;
    private TracerInterface $tracer;
    private GraftCodeDatadogLogger $logger;
    private GraftcodeInstrumentation $instrumentation;
    private ?CallFlusher $callFlusher = null;
    private ?ScopeInterface $globals;
    private ?Closure $onClose;
    private bool $closed = false;

    /**
     * @param callable(GraftCodeDatadogConnectorPipeline): void|null $onClose
     */
    public function __construct(
        GraftCodeDatadogConnectorOptions $options,
        ?SpanExporterInterface $spanExporter = null,
        ?LogRecordExporterInterface $logRecordExporter = null,
        ?callable $onClose = null
    ) {
        $options->validate();
        $this->serviceName = trim($options->serviceName);
        $this->endpoint = $options->resolveEndpoint();
        $this->onClose = $onClose === null ? null : Closure::fromCallable($onClose);

        $resource = $this->buildResource($options);
        $this->tracerProvider = TracerProvider::builder()
            ->setResource($resource)
            ->addSpanProcessor($this->spanProcessor($spanExporter))
            ->build();
        $this->loggerProvider = LoggerProvider::builder()
            ->setResource($resource)
            ->addLogRecordProcessor($this->logRecordProcessor($logRecordExporter))
            ->build();

        $this->globals = Sdk::builder()
            ->setTracerProvider($this->tracerProvider)
            ->setLoggerProvider($this->loggerProvider)
            ->setPropagator(new MultiTextMapPropagator([
                TraceContextPropagator::getInstance(),
                BaggagePropagator::getInstance(),
            ]))
            ->buildAndRegisterGlobal();

        $this->tracer = $this->tracerProvider->getTracer($this->serviceName, self::VERSION);
        $this->logger = new GraftCodeDatadogLogger(
            $this->serviceName,
            $this->loggerProvider->getLogger($this->serviceName, self::VERSION),
            (int) $options->getMinimumLogLevel(),
            $options->enableConsoleLogging ? fopen('php://stdout', 'wb') : null
        );
        $this->instrumentation = GraftcodeInstrumentation::attach(
            $this->tracerProvider,
            $options->getMinimumLogLevel(),
            $this->loggerProvider
        );

        if ($options->resolveFlushAfterCalls()) {
            $this->callFlusher = new CallFlusher($this);
            HypertubeBroker::attach($this->callFlusher);
        }

        register_shutdown_function(function (): void {
            $this->close();
        });
    }

    public function getServiceName(): string
    {
        return $this->serviceName;
    }

    public function getEndpoint(): string
    {
        return $this->endpoint;
    }

    public function getTracer(): TracerInterface
    {
        return $this->tracer;
    }

    public function getLogger(): GraftCodeDatadogLogger
    {
        return $this->logger;
    }

    public function getTracerProvider(): TracerProvider
    {
        return $this->tracerProvider;
    }

    public function getLoggerProvider(): LoggerProvider
    {
        return $this->loggerProvider;
    }

    public function flushesAfterCalls(): bool
    {
        return $this->callFlusher !== null;
    }

    /**
     * Runs $work in a new current span; a throwing $work marks the span failed and rethrows.
     *
     * @template T
     * @param callable(SpanInterface): T $work
     * @param array<string, mixed> $attributes
     * @return T
     */
    public function inSpan(string $name, callable $work, int $kind = SpanKind::KIND_INTERNAL, array $attributes = [])
    {
        $span = $this->tracer->spanBuilder($name)->setSpanKind($kind)->setAttributes($attributes)->startSpan();
        $scope = $span->activate();
        try {
            return $work($span);
        } catch (Throwable $exception) {
            $span->recordException($exception);
            $span->setStatus(StatusCode::STATUS_ERROR, $exception->getMessage());

            throw $exception;
        } finally {
            $scope->detach();
            $span->end();
        }
    }

    public function flush(): bool
    {
        if ($this->closed) {
            return true;
        }

        $traces = $this->tracerProvider->forceFlush();
        $logs = $this->loggerProvider->forceFlush();

        return $traces && $logs;
    }

    public function close(): void
    {
        if ($this->closed) {
            return;
        }

        $this->closed = true;
        if ($this->callFlusher !== null) {
            HypertubeBroker::detach($this->callFlusher);
        }

        $this->instrumentation->dispose();
        $this->tracerProvider->shutdown();
        $this->loggerProvider->shutdown();
        $this->globals?->detach();
        $this->globals = null;

        if ($this->onClose !== null) {
            ($this->onClose)($this);
        }
    }

    public function shutdown(): void
    {
        $this->close();
    }

    public function isClosed(): bool
    {
        return $this->closed;
    }

    private function buildResource(GraftCodeDatadogConnectorOptions $options): ResourceInfo
    {
        $attributes = ['service.name' => $this->serviceName];
        $environment = $options->resolveEnvironment();
        $version = $options->resolveVersion();
        if ($environment !== null) {
            $attributes['deployment.environment.name'] = $environment;
        }
        if ($version !== null) {
            $attributes['service.version'] = $version;
        }

        return ResourceInfoFactory::defaultResource()->merge(ResourceInfo::create(Attributes::create($attributes)));
    }

    private function spanProcessor(?SpanExporterInterface $exporter): SpanProcessorInterface
    {
        if ($exporter !== null) {
            return new SimpleSpanProcessor($exporter);
        }

        $transport = (new OtlpHttpTransportFactory())->create($this->endpoint . self::TRACES_PATH, self::PROTOBUF);

        return new BatchSpanProcessor(new SpanExporter($transport), Clock::getDefault());
    }

    private function logRecordProcessor(?LogRecordExporterInterface $exporter): LogRecordProcessorInterface
    {
        if ($exporter !== null) {
            return new SimpleLogRecordProcessor($exporter);
        }

        $transport = (new OtlpHttpTransportFactory())->create($this->endpoint . self::LOGS_PATH, self::PROTOBUF);

        return new BatchLogRecordProcessor(new LogsExporter($transport), Clock::getDefault());
    }
}
