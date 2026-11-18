<?php

declare(strict_types=1);

namespace Graftcode\Instrumentation\Tests;

use Graftcode\Instrumentation\GraftcodeInstrumentation;
use Graftcode\Instrumentation\Tests\Support\Harness;
use HypertubePhpUtils\abstractions\HypertubeBroker;
use HypertubePhpUtils\abstractions\HypertubeCallContext;
use HypertubePhpUtils\logging\HypertubeLogger;
use HypertubePhpUtils\logging\LogsLevel;
use OpenTelemetry\SDK\Trace\ImmutableSpan;
use PHPUnit\Framework\TestCase;
use RuntimeException;

final class GraftcodeInstrumentationTest extends TestCase
{
    private ?Harness $harness = null;
    private int $savedLevel;

    protected function setUp(): void
    {
        $this->savedLevel = HypertubeLogger::getMinimumLevel();
        HypertubeLogger::setMinimumLevel(LogsLevel::OFF);
    }

    protected function tearDown(): void
    {
        $this->harness?->close();
        HypertubeLogger::setMinimumLevel($this->savedLevel);
    }

    public function testTheBrokerOpensAndClosesTheInvokeSpan(): void
    {
        $this->harness = new Harness();
        $context = new HypertubeCallContext();

        HypertubeBroker::onExecute($context, Harness::fetch(), Harness::inMemory());
        HypertubeBroker::onResponse($context, Harness::value('ok'));

        self::assertSame([], $context->items);
        self::assertSame('Graft SomeLib inmemory', $this->single()->getName());
    }

    public function testRecordsHypertubeLogsAsEventsOfTheCurrentSpan(): void
    {
        $this->harness = new Harness();
        $callSpan = $this->harness->onHandle(Harness::fetch());

        HypertubeLogger::getLogger()->log(LogsLevel::INFO, 'Hypertube handling {CommandType}', 'InvokeStaticMethod');
        HypertubeLogger::getLogger()->logException(new RuntimeException('boom'), 'Receiver failed');
        $callSpan->complete(null, null);

        $events = $this->single()->getEvents();
        self::assertCount(2, $events);
        self::assertSame('Hypertube handling InvokeStaticMethod', $events[0]->getName());
        self::assertSame('Information', $events[0]->getAttributes()->get('log.level'));
        self::assertSame('InvokeStaticMethod', $events[0]->getAttributes()->get('CommandType'));
        self::assertSame('exception', $events[1]->getName());
        self::assertSame('Receiver failed', $events[1]->getAttributes()->get('log.message'));
        self::assertSame(RuntimeException::class, $events[1]->getAttributes()->get('exception.type'));
        self::assertSame('boom', $events[1]->getAttributes()->get('exception.message'));
    }

    public function testRecordsNoEventsOutsideASpan(): void
    {
        $this->harness = new Harness();

        self::assertFalse($this->harness->listener->isRequested());
        HypertubeLogger::getLogger()->log(LogsLevel::ERROR, 'outside');

        self::assertSame([], $this->harness->finishedSpans());
    }

    public function testEmitsHypertubeLogsThroughTheLoggerProviderAboveTheMinimumLevel(): void
    {
        $this->harness = new Harness(true, LogsLevel::INFO);
        $callSpan = $this->harness->onHandle(Harness::fetch());

        HypertubeLogger::getLogger()->log(LogsLevel::DEBUG, 'hidden');
        HypertubeLogger::getLogger()->log(LogsLevel::WARNING, 'WebSocket send failed {@Transport}', ['transport' => 'websocket']);
        $callSpan->complete(null, null);

        $logs = $this->harness->logs();
        self::assertCount(1, $logs);
        self::assertSame(13, $logs[0]->getSeverityNumber());
        self::assertSame('Warning', $logs[0]->getSeverityText());
        self::assertSame(GraftcodeInstrumentation::LOGGER_NAME, $logs[0]->getInstrumentationScope()->getName());
        self::assertStringStartsWith('WebSocket send failed ', (string) $logs[0]->getBody());
        self::assertSame($logs[0]->getBody(), $logs[0]->getAttributes()->get('{OriginalFormat}'));
        self::assertSame($callSpan->getSpan()->getContext()->getTraceId(), $logs[0]->getSpanContext()->getTraceId());
        self::assertCount(0, $this->single()->getEvents());
    }

    public function testDisposeDetachesFromTheBrokerAndTheLogger(): void
    {
        $this->harness = new Harness();
        $this->harness->listener->dispose();
        $context = new HypertubeCallContext();

        HypertubeBroker::onExecute($context, Harness::fetch(), Harness::inMemory());

        self::assertSame([], $context->items);
        self::assertNotContains($this->harness->listener, HypertubeLogger::getListeners());
    }

    private function single(): ImmutableSpan
    {
        $spans = $this->harness->finishedSpans();
        self::assertCount(1, $spans);

        return $spans[0];
    }
}
