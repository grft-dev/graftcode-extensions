<?php

declare(strict_types=1);

namespace Graftcode\Instrumentation\Tests;

use Graftcode\Instrumentation\CallSpan;
use Graftcode\Instrumentation\GraftcodeInstrumentation;
use Graftcode\Instrumentation\GraftcodeTelemetrySettings;
use Graftcode\Instrumentation\Tests\Support\Harness;
use HypertubePhpUtils\logging\LogsRedactor;
use HypertubePhpUtils\type\CommandType;
use OpenTelemetry\API\Trace\Span;
use OpenTelemetry\API\Trace\StatusCode;
use OpenTelemetry\SDK\Trace\ImmutableSpan;
use PHPUnit\Framework\Attributes\DataProvider;
use PHPUnit\Framework\TestCase;
use RuntimeException;

final class CallSpanTest extends TestCase
{
    private const TRACE_ID = '0af7651916cd43dd8448eb211c80319c';
    private const PARENT_SPAN_ID = 'b7ad6b7169203331';
    private const TRACE_PARENT = '00-' . self::TRACE_ID . '-' . self::PARENT_SPAN_ID . '-01';

    private Harness $harness;

    protected function setUp(): void
    {
        $this->harness = new Harness();
    }

    protected function tearDown(): void
    {
        $this->harness->close();
    }

    public function testInvokeOpensADependencySpanNamedAfterTheTarget(): void
    {
        $this->harness->onInvoke(Harness::fetch(), Harness::inMemory())->complete(Harness::value('ok'), null);

        $stopped = $this->single();
        self::assertSame(GraftcodeInstrumentation::INSTRUMENTATION_NAME, $stopped->getInstrumentationScope()->getName());
        self::assertSame('Graft SomeLib inmemory', $stopped->getName());
        $attributes = $stopped->getAttributes()->toArray();
        self::assertSame('Graft', $attributes[CallSpan::DEPENDENCY_TYPE]);
        self::assertSame('SomeLib inmemory', $attributes[CallSpan::DEPENDENCY_TARGET]);
        self::assertSame('SomeLib.Service.Fetch', $attributes['hypertube.operation']);
        self::assertSame('InvokeStaticMethod', $attributes['hypertube.command_type']);
        self::assertSame('Netcore', $attributes['hypertube.runtime']);
        self::assertSame('InMemory', $attributes['hypertube.connection_type']);
        self::assertSame('inmemory', $attributes['hypertube.target']);
        self::assertSame(StatusCode::STATUS_OK, $stopped->getStatus()->getCode());
        self::assertFalse(Span::getCurrent()->getContext()->isValid());
    }

    public function testWritesTheSpanIntoTheHeadersAndRestoresThem(): void
    {
        $connection = Harness::inMemory();
        $connection->setThreadLocalHeaders(['tenant' => 't1']);

        $callSpan = $this->harness->onInvoke(Harness::fetch(), $connection);

        self::assertSame(self::traceParentOf($callSpan), $connection->getHeaders()[CallSpan::TRACE_PARENT_HEADER]);
        self::assertSame($callSpan->getSpan()->getContext()->getTraceId(), $connection->getHeaders()[CallSpan::TRACE_ID_HEADER]);
        self::assertSame('t1', $connection->getHeaders()['tenant']);

        $callSpan->complete(null, null);

        self::assertArrayNotHasKey(CallSpan::TRACE_PARENT_HEADER, $connection->getHeaders());
        self::assertArrayNotHasKey(CallSpan::TRACE_ID_HEADER, $connection->getHeaders());
        self::assertSame('t1', $connection->getHeaders()['tenant']);
    }

    public function testDoesNotReplaceATraceTheCallerAlreadySet(): void
    {
        $connection = Harness::inMemory();
        $connection->setHeaders([CallSpan::TRACE_PARENT_HEADER => self::TRACE_PARENT]);

        $callSpan = $this->harness->onInvoke(Harness::fetch(), $connection);

        self::assertSame(self::TRACE_ID, $callSpan->getSpan()->getContext()->getTraceId());
        self::assertSame(self::TRACE_PARENT, $connection->getHeaders()[CallSpan::TRACE_PARENT_HEADER]);
        $callSpan->complete(null, null);
        self::assertSame(self::PARENT_SPAN_ID, $this->single()->getParentSpanId());
    }

    public function testPropagatesTheInnerSpanOfANestedInvokeAndRestoresTheOuterOne(): void
    {
        $connection = Harness::inMemory();

        $outer = $this->harness->onInvoke(Harness::fetch(), $connection);
        $inner = $this->harness->onInvoke(Harness::fetch(), $connection);

        self::assertSame(self::traceParentOf($inner), $connection->getHeaders()[CallSpan::TRACE_PARENT_HEADER]);

        $inner->complete(null, null);
        self::assertSame(self::traceParentOf($outer), $connection->getHeaders()[CallSpan::TRACE_PARENT_HEADER]);
        self::assertSame($outer->getSpan()->getContext()->getSpanId(), Span::getCurrent()->getContext()->getSpanId());

        $outer->complete(null, null);
        self::assertArrayNotHasKey(CallSpan::TRACE_PARENT_HEADER, $connection->getHeaders());
        self::assertFalse(Span::getCurrent()->getContext()->isValid());
        self::assertSame($outer->getSpan()->getContext()->getSpanId(), $this->harness->finishedSpans()[0]->getParentSpanId());
    }

    public function testLeavesTheHeadersAloneWhenAutoInjectIsDisabled(): void
    {
        GraftcodeTelemetrySettings::setAutoInjectEnabled(false);
        $connection = Harness::inMemory();

        $callSpan = $this->harness->onInvoke(Harness::fetch(), $connection);

        self::assertArrayNotHasKey(CallSpan::TRACE_PARENT_HEADER, $connection->getHeaders());
        $callSpan->complete(null, null);
    }

    public function testStopsAFailedCallWithARedactedMessage(): void
    {
        $this->harness->onInvoke(Harness::fetch(), Harness::inMemory())->complete(null, new RuntimeException('Password=hunter2'));

        $stopped = $this->single();
        self::assertSame(StatusCode::STATUS_ERROR, $stopped->getStatus()->getCode());
        self::assertSame(LogsRedactor::PLACEHOLDER, $stopped->getStatus()->getDescription());
        self::assertSame(RuntimeException::class, $stopped->getAttributes()->get('exception.type'));
        self::assertSame(LogsRedactor::PLACEHOLDER, $stopped->getAttributes()->get('exception.message'));
        self::assertCount(0, $stopped->getEvents());
    }

    public function testReportsAnExceptionResponseAsACompletedCall(): void
    {
        $response = Harness::of(CommandType::EXCEPTION(), 0, 'trace', 'ArgumentError', 'bad input');
        $this->harness->onHandle(Harness::fetch())->complete($response, null);

        $stopped = $this->single();
        self::assertSame(StatusCode::STATUS_OK, $stopped->getStatus()->getCode());
        self::assertNull($stopped->getAttributes()->get('exception.type'));
    }

    /**
     * @return iterable<string, array{string}>
     */
    public static function transportCommands(): iterable
    {
        foreach (['INVOKE_DELEGATE', 'PASS_BY_VALUE', 'REGISTER_FOR_UPDATE', 'VALUE_FOR_UPDATE'] as $type) {
            yield $type => [$type];
        }
    }

    #[DataProvider('transportCommands')]
    public function testOpensNoSpanForTransportCommands(string $type): void
    {
        $command = Harness::of(CommandType::$type(), 'x');

        self::assertNull($this->harness->onInvoke($command, Harness::inMemory()));
        self::assertNull($this->harness->onHandle($command));
        self::assertSame([], $this->harness->finishedSpans());
    }

    public function testOpensNoSpanForTelemetrySetupCalls(): void
    {
        $setup = Harness::of(
            CommandType::INVOKE_STATIC_METHOD(),
            Harness::of(CommandType::GET_TYPE(), Harness::value('App\\TelemetrySetup')),
            Harness::value('initialize')
        );

        self::assertNull($this->harness->onHandle($setup));
    }

    public function testGivesAnUnrecordedSpanNoAttributesButStillPropagatesIt(): void
    {
        $this->harness->close();
        $this->harness = new Harness(false);
        $connection = Harness::inMemory();

        $callSpan = $this->harness->onInvoke(Harness::fetch(), $connection);

        self::assertFalse($callSpan->getSpan()->isRecording());
        self::assertSame(self::traceParentOf($callSpan), $connection->getHeaders()[CallSpan::TRACE_PARENT_HEADER]);
        $callSpan->complete(null, null);
        self::assertSame([], $this->harness->finishedSpans());
    }

    public function testNamesTheHandleSpanAfterTheOperationAndParentsItOnTheIncomingTraceParent(): void
    {
        $this->harness->onHandle(Harness::fetch(), [CallSpan::TRACE_PARENT_HEADER => self::TRACE_PARENT])->complete(null, null);

        $stopped = $this->single();
        self::assertSame('SomeLib.Service.Fetch', $stopped->getName());
        self::assertSame(self::TRACE_ID, $stopped->getTraceId());
        self::assertSame(self::PARENT_SPAN_ID, $stopped->getParentSpanId());
        self::assertNull($stopped->getAttributes()->get(CallSpan::DEPENDENCY_TYPE));
    }

    public function testReadsTheIncomingTraceParentRegardlessOfHeaderCase(): void
    {
        $this->harness->onHandle(Harness::fetch(), ['TraceParent' => self::TRACE_PARENT])->complete(null, null);

        self::assertSame(self::PARENT_SPAN_ID, $this->single()->getParentSpanId());
    }

    public function testFallsBackToTheBareTraceIdOnTheHandleSide(): void
    {
        $this->harness->onHandle(Harness::fetch(), [CallSpan::TRACE_ID_HEADER => self::TRACE_ID])->complete(null, null);

        $stopped = $this->single();
        self::assertSame(self::TRACE_ID, $stopped->getTraceId());
        self::assertNotSame('0000000000000000', $stopped->getParentSpanId());
    }

    public function testNamesTheTargetOfLaterCallsAfterALoadedLibraryAndItsComposerVersion(): void
    {
        $directory = sys_get_temp_dir() . DIRECTORY_SEPARATOR . 'graft-lib-' . bin2hex(random_bytes(4)) . DIRECTORY_SEPARATOR . 'TestGgPhpLib';
        mkdir($directory, 0777, true);
        file_put_contents($directory . DIRECTORY_SEPARATOR . 'composer.json', '{"name":"testgg/testggphplib","version":"1.2.3"}');

        try {
            $connection = Harness::inMemory();
            $this->harness->onInvoke(Harness::of(CommandType::LOAD_LIBRARY(), Harness::value($directory)), $connection)->complete(null, null);
            $fetchRandom = Harness::of(
                CommandType::INVOKE_STATIC_METHOD(),
                Harness::of(CommandType::GET_TYPE(), Harness::value('TestGgPhpLib\\RandomService')),
                Harness::value('fetch')
            );
            $this->harness->onInvoke($fetchRandom, $connection)->complete(null, null);

            $spans = $this->harness->finishedSpans();
            self::assertSame('Graft TestGgPhpLib 1.2.3 inmemory', $spans[1]->getName());
            self::assertSame('TestGgPhpLib.RandomService.fetch', $spans[1]->getAttributes()->get('hypertube.operation'));
        } finally {
            unlink($directory . DIRECTORY_SEPARATOR . 'composer.json');
            rmdir($directory);
            rmdir(dirname($directory));
        }
    }

    public function testNamesTheTargetAfterTheRootNamespaceWhenNoLibraryWasLoaded(): void
    {
        $fetchRandom = Harness::of(
            CommandType::INVOKE_STATIC_METHOD(),
            Harness::of(CommandType::GET_TYPE(), Harness::value('TestGgPhpLib\\RandomService')),
            Harness::value('fetch')
        );
        $this->harness->onInvoke($fetchRandom, Harness::inMemory())->complete(null, null);

        self::assertSame('Graft TestGgPhpLib inmemory', $this->single()->getName());
    }

    public function testCapturesTheBodiesWhenEnabled(): void
    {
        GraftcodeTelemetrySettings::setBodyCaptureEnabled(true);

        $this->harness->onInvoke(Harness::fetch(), Harness::inMemory())->complete(Harness::value('ok'), null);

        $attributes = $this->single()->getAttributes();
        self::assertSame(
            '{"runtime":"Netcore","type":"InvokeStaticMethod","payload":[{"runtime":"Netcore","type":"GetType","payload":[{"runtime":"Netcore","type":"Value","payload":["SomeLib.Service"]}]},{"runtime":"Netcore","type":"Value","payload":["Fetch"]}]}',
            $attributes->get(CallSpan::REQUEST_BODY)
        );
        self::assertSame('{"runtime":"Netcore","type":"Value","payload":["ok"]}', $attributes->get(CallSpan::RESPONSE_BODY));
        self::assertNull($attributes->get(CallSpan::REQUEST_BODY_TRUNCATED));
    }

    public function testCompletingTheCallContextStopsItsSpans(): void
    {
        $context = new \HypertubePhpUtils\abstractions\HypertubeCallContext();
        $this->harness->listener->onExecute($context, Harness::fetch(), Harness::inMemory());

        $context->dispose();

        self::assertSame(StatusCode::STATUS_OK, $this->single()->getStatus()->getCode());
    }

    private function single(): ImmutableSpan
    {
        $spans = $this->harness->finishedSpans();
        self::assertCount(1, $spans);

        return $spans[0];
    }

    private static function traceParentOf(CallSpan $callSpan): string
    {
        $context = $callSpan->getSpan()->getContext();

        return '00-' . $context->getTraceId() . '-' . $context->getSpanId() . '-' . sprintf('%02x', $context->getTraceFlags());
    }
}
