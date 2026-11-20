<?php

declare(strict_types=1);

namespace Graftcode\Instrumentation;

use HypertubePhpUtils\abstractions\HypertubeCallContext;
use HypertubePhpUtils\CommandInterface;
use HypertubePhpUtils\connectiondata\IConnectionData;
use HypertubePhpUtils\logging\LogsCommandResolver;
use HypertubePhpUtils\logging\LogsRedactor;
use HypertubePhpUtils\type\CommandType;
use OpenTelemetry\API\Trace\Propagation\TraceContextPropagator;
use OpenTelemetry\API\Trace\Span;
use OpenTelemetry\API\Trace\SpanContext;
use OpenTelemetry\API\Trace\SpanInterface;
use OpenTelemetry\API\Trace\SpanKind;
use OpenTelemetry\API\Trace\StatusCode;
use OpenTelemetry\API\Trace\TraceFlags;
use OpenTelemetry\API\Trace\TracerInterface;
use OpenTelemetry\Context\Context;
use OpenTelemetry\Context\ContextInterface;
use OpenTelemetry\Context\ScopeInterface;
use Throwable;

final class CallSpan
{
    public const DEPENDENCY_TYPE = 'graft.dependency.type';
    public const DEPENDENCY_TARGET = 'graft.dependency.target';
    public const BRIDGE_DEPENDENCY_TYPE = 'Graft';
    public const REQUEST_BODY = 'hypertube.request.body';
    public const RESPONSE_BODY = 'hypertube.response.body';
    public const REQUEST_BODY_TRUNCATED = 'hypertube.request.body.truncated';
    public const RESPONSE_BODY_TRUNCATED = 'hypertube.response.body.truncated';
    public const TRACE_PARENT_HEADER = 'traceparent';
    public const TRACE_STATE_HEADER = 'tracestate';
    public const TRACE_ID_HEADER = 'traceid';

    private const SKIP_COMMAND_TYPES = [
        CommandType::PASS_BY_VALUE,
        CommandType::REGISTER_FOR_UPDATE,
        CommandType::VALUE_FOR_UPDATE,
        CommandType::INVOKE_DELEGATE,
    ];

    private SpanInterface $span;
    private ScopeInterface $scope;
    private ?IConnectionData $injectedInto = null;
    /** @var array<string, string>|null */
    private ?array $previousHeaders = null;
    private bool $completed = false;

    private function __construct(SpanInterface $span)
    {
        $this->span = $span;
        $this->scope = $span->activate();
    }

    public function getSpan(): SpanInterface
    {
        return $this->span;
    }

    public static function startInvoke(TracerInterface $tracer, CommandInterface $command, IConnectionData $connectionData): ?CallSpan
    {
        $operation = self::resolveOperation($command);
        if ($operation === null) {
            return null;
        }

        LibraryCatalog::record($command, $connectionData);
        $target = LibraryCatalog::describeTarget($command, $connectionData);
        $span = self::start($tracer, 'Graft ' . $target, $connectionData->getHeaders());

        if ($span->isRecording()) {
            $span->setAttribute('hypertube.runtime', LogsCommandResolver::getRuntimeName($command->getRuntimeName()));
            $span->setAttribute('hypertube.command_type', LogsCommandResolver::getCommandTypeName($command->getCommandType()));
            $span->setAttribute('hypertube.connection_type', LibraryCatalog::getConnectionTypeName($connectionData->getConnectionType()));
            $span->setAttribute(self::DEPENDENCY_TYPE, self::BRIDGE_DEPENDENCY_TYPE);
            $span->setAttribute(self::DEPENDENCY_TARGET, LogsRedactor::redactValue($target));
            $span->setAttribute('hypertube.operation', LogsRedactor::redactValue($operation));
            $span->setAttribute('hypertube.target', LogsRedactor::redactValue(LogsRedactor::redactTarget($connectionData->getHostname())));
        }

        $callSpan = new CallSpan($span);
        $callSpan->capture($command, self::REQUEST_BODY, self::REQUEST_BODY_TRUNCATED);
        if (GraftcodeTelemetrySettings::isAutoInjectEnabled()) {
            $callSpan->injectInto($connectionData);
        }

        return $callSpan;
    }

    /**
     * @param array<string, string> $headers
     */
    public static function startHandle(TracerInterface $tracer, CommandInterface $command, array $headers): ?CallSpan
    {
        $operation = self::resolveOperation($command);
        if ($operation === null) {
            return null;
        }

        $callSpan = new CallSpan(self::start($tracer, $operation, $headers));
        $callSpan->capture($command, self::REQUEST_BODY, self::REQUEST_BODY_TRUNCATED);

        return $callSpan;
    }

    public static function completeSpans(HypertubeCallContext $context, ?CommandInterface $response, ?Throwable $exception): void
    {
        for ($i = count($context->items) - 1; $i >= 0; $i--) {
            $item = $context->items[$i];
            if ($item instanceof CallSpan) {
                $item->complete($response, $exception);
                array_splice($context->items, $i, 1);
            }
        }
    }

    public function complete(?CommandInterface $response, ?Throwable $exception): void
    {
        if ($this->completed) {
            return;
        }

        $this->completed = true;
        try {
            if ($response !== null) {
                $this->capture($response, self::RESPONSE_BODY, self::RESPONSE_BODY_TRUNCATED);
            }

            if ($exception === null) {
                $this->span->setStatus(StatusCode::STATUS_OK);
            } else {
                $message = LogsRedactor::redactValue($exception->getMessage());
                $this->span->setAttribute('exception.type', get_class($exception));
                $this->span->setAttribute('exception.message', $message);
                $this->span->setStatus(StatusCode::STATUS_ERROR, $message);
            }

            if ($this->injectedInto !== null) {
                $this->injectedInto->setThreadLocalHeaders($this->previousHeaders ?? []);
            }
        } finally {
            $this->scope->detach();
            $this->span->end();
        }
    }

    public function dispose(): void
    {
        $this->complete(null, null);
    }

    private static function resolveOperation(CommandInterface $command): ?string
    {
        if (in_array($command->getCommandType()->getValue(), self::SKIP_COMMAND_TYPES, true)) {
            return null;
        }

        $operation = OperationName::resolve($command);

        return substr(strtolower($operation), -strlen('telemetrysetup.initialize')) === 'telemetrysetup.initialize'
            ? null
            : $operation;
    }

    /**
     * @param array<string, string> $headers
     */
    private static function start(TracerInterface $tracer, string $name, array $headers): SpanInterface
    {
        return $tracer->spanBuilder($name)
            ->setParent(self::extractParentContext($headers))
            ->setSpanKind(SpanKind::KIND_INTERNAL)
            ->startSpan();
    }

    /**
     * @param array<string, string> $headers
     */
    private static function extractParentContext(array $headers): ContextInterface
    {
        $extracted = TraceContextPropagator::getInstance()->extract($headers, null, Context::getRoot());
        if (Span::fromContext($extracted)->getContext()->isValid()) {
            return $extracted;
        }

        $traceId = self::readTraceId(self::header($headers, self::TRACE_ID_HEADER));
        if ($traceId === null || $traceId === Span::getCurrent()->getContext()->getTraceId()) {
            return Context::getCurrent();
        }

        $remote = SpanContext::createFromRemoteParent($traceId, bin2hex(random_bytes(8)), TraceFlags::SAMPLED);

        return Span::wrap($remote)->storeInContext(Context::getRoot());
    }

    /**
     * @param array<string, string> $headers
     */
    private static function header(array $headers, string $key): ?string
    {
        if (isset($headers[$key])) {
            return $headers[$key];
        }

        foreach ($headers as $entryKey => $value) {
            if (strcasecmp((string) $entryKey, $key) === 0) {
                return $value;
            }
        }

        return null;
    }

    private static function readTraceId(?string $raw): ?string
    {
        if ($raw === null) {
            return null;
        }

        $hex = strtolower(str_replace('-', '', trim($raw)));

        return preg_match('/^[0-9a-f]{32}$/', $hex) === 1 && trim($hex, '0') !== '' ? $hex : null;
    }

    private function injectInto(IConnectionData $connectionData): void
    {
        $local = $connectionData->getThreadLocalHeaders();
        $spanContext = $this->span->getContext();
        if (!$spanContext->isValid()) {
            return;
        }

        $merged = $connectionData->getHeaders();
        if (isset($merged[self::TRACE_PARENT_HEADER]) && !isset($local[self::TRACE_PARENT_HEADER])) {
            return;
        }

        $headers = $local;
        unset($headers[self::TRACE_STATE_HEADER]);
        TraceContextPropagator::getInstance()->inject($headers, null, $this->span->storeInContext(Context::getRoot()));
        $headers[self::TRACE_ID_HEADER] = $spanContext->getTraceId();

        $connectionData->setThreadLocalHeaders($headers);
        $this->injectedInto = $connectionData;
        $this->previousHeaders = $local;
    }

    private function capture(CommandInterface $command, string $bodyKey, string $truncatedKey): void
    {
        if (!$this->span->isRecording() || !GraftcodeTelemetrySettings::isBodyCaptureEnabled()) {
            return;
        }

        $rendered = BodyCapture::render($command, GraftcodeTelemetrySettings::getBodyMaxCharacters());
        if ($rendered === null) {
            return;
        }

        $this->span->setAttribute($bodyKey, $rendered['body']);
        if ($rendered['truncated']) {
            $this->span->setAttribute($truncatedKey, true);
        }
    }
}
