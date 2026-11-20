<?php

declare(strict_types=1);

namespace Graftcode\Telemetry\Datadog\Tests;

use Graftcode\Telemetry\Datadog\GraftCodeDatadogConnector;
use Graftcode\Telemetry\Datadog\GraftCodeDatadogConnectorOptions;
use Graftcode\Telemetry\Datadog\Tests\Support\TelemetryTestCase;
use HypertubePhpUtils\abstractions\HypertubeBroker;
use HypertubePhpUtils\abstractions\HypertubeCallContext;
use HypertubePhpUtils\Command;
use HypertubePhpUtils\connectiondata\InMemoryConnectionData;
use HypertubePhpUtils\logging\HypertubeLogger;
use HypertubePhpUtils\logging\LogsLevel;
use HypertubePhpUtils\RuntimeName;
use HypertubePhpUtils\type\CommandType;
use InvalidArgumentException;
use OpenTelemetry\API\Globals;
use RuntimeException;

final class GraftCodeDatadogConnectorTest extends TelemetryTestCase
{
    public function testReturnsTheRunningPipelineWhenStartedAgainForTheSameService(): void
    {
        $telemetry = $this->startTelemetry('Orders');

        self::assertSame($telemetry, GraftCodeDatadogConnector::start('Orders'));
    }

    public function testRefusesToStartASecondServiceInTheSameProcess(): void
    {
        $this->startTelemetry('Orders');

        $this->expectException(InvalidArgumentException::class);
        $this->expectExceptionMessage('already started for Orders');
        GraftCodeDatadogConnector::start('Billing');
    }

    public function testRequiresAServiceName(): void
    {
        $this->expectException(InvalidArgumentException::class);
        $this->expectExceptionMessage('serviceName');
        GraftCodeDatadogConnector::start(null, null, new GraftCodeDatadogConnectorOptions([]));
    }

    public function testCanBeStartedAgainAfterClose(): void
    {
        $this->startTelemetry('Orders')->close();

        self::assertNull(GraftCodeDatadogConnector::getActive());
        self::assertSame('Billing', $this->startTelemetry('Billing')->getServiceName());
    }

    public function testDescribesTheServiceEnvironmentAndVersionOnTheResource(): void
    {
        $telemetry = $this->startTelemetry('Orders', ['environment' => 'local', 'version' => '1.2.3']);
        $telemetry->inSpan('work', static fn () => null);

        $attributes = $this->finishedSpans()[0]->getResource()->getAttributes();
        self::assertSame('Orders', $attributes->get('service.name'));
        self::assertSame('local', $attributes->get('deployment.environment.name'));
        self::assertSame('1.2.3', $attributes->get('service.version'));
    }

    public function testAttachesGraftcodeInstrumentationToTheHypertubeBrokerUntilClosed(): void
    {
        $telemetry = $this->startTelemetry();
        $context = new HypertubeCallContext();

        HypertubeBroker::onExecute($context, self::fetch(), new InMemoryConnectionData());
        self::assertCount(1, $context->items);
        $context->dispose();

        $telemetry->close();
        $context = new HypertubeCallContext();
        HypertubeBroker::onExecute($context, self::fetch(), new InMemoryConnectionData());
        self::assertSame([], $context->items);
    }

    public function testFlushesAfterEachHandledCallWhenHostedAfterTheInstrumentationHasCompletedItsSpan(): void
    {
        $telemetry = $this->startTelemetry('Orders', ['flushAfterCalls' => true]);
        self::assertTrue($telemetry->flushesAfterCalls());

        $context = new HypertubeCallContext();
        HypertubeBroker::onProcess($context, self::fetch());
        HypertubeBroker::onProcessed($context, self::value('ok'));

        self::assertSame(1, $this->spans->flushCount);
        self::assertCount(1, $this->finishedSpans());

        HypertubeBroker::onHandlingError(new HypertubeCallContext(), new RuntimeException('boom'));
        self::assertSame(2, $this->spans->flushCount);

        $telemetry->close();
        $flushes = $this->spans->flushCount;
        HypertubeBroker::onProcessed(new HypertubeCallContext(), self::value('ok'));
        self::assertSame($flushes, $this->spans->flushCount);
    }

    public function testExportsHypertubeLogsAsLogRecordsCorrelatedWithTheCurrentSpan(): void
    {
        $telemetry = $this->startTelemetry();

        $telemetry->inSpan('call', static function (): void {
            HypertubeLogger::getLogger()->log(
                LogsLevel::INFO,
                'Hypertube invoking {CommandType} on the {RuntimeName} runtime',
                'InvokeStaticMethod',
                'Php'
            );
        });

        $records = array_values(array_filter(
            $this->emittedLogs(),
            static fn ($log) => $log->getInstrumentationScope()->getName() === 'Hypertube'
        ));
        self::assertCount(1, $records);
        self::assertSame('Hypertube invoking InvokeStaticMethod on the Php runtime', $records[0]->getBody());
        self::assertSame('InvokeStaticMethod', $records[0]->getAttributes()->get('CommandType'));
        self::assertSame('Php', $records[0]->getAttributes()->get('RuntimeName'));
        self::assertSame($this->finishedSpans()[0]->getTraceId(), $records[0]->getSpanContext()->getTraceId());
        self::assertSame($this->finishedSpans()[0]->getSpanId(), $records[0]->getSpanContext()->getSpanId());
    }

    public function testLeavesExportingToTheBatchProcessorsInARegularPhpProcess(): void
    {
        $options = new GraftCodeDatadogConnectorOptions([]);
        $options->enableConsoleLogging = false;
        $telemetry = GraftCodeDatadogConnector::start('Orders', null, $options, $this->spans, $this->logs);

        self::assertFalse($telemetry->flushesAfterCalls());
    }

    public function testPropagatesW3CTraceContext(): void
    {
        $telemetry = $this->startTelemetry();
        $carrier = [];

        $telemetry->inSpan('outgoing', static function () use (&$carrier): void {
            Globals::propagator()->inject($carrier);
        });

        self::assertStringStartsWith('00-' . $this->finishedSpans()[0]->getTraceId() . '-', $carrier['traceparent']);
    }

    public function testMarksAFailedSpanAndRethrows(): void
    {
        $telemetry = $this->startTelemetry();

        try {
            $telemetry->inSpan('work', static function (): void {
                throw new RuntimeException('boom');
            });
            self::fail('The exception was swallowed.');
        } catch (RuntimeException $exception) {
            self::assertSame('boom', $exception->getMessage());
        }

        self::assertSame('Error', $this->finishedSpans()[0]->getStatus()->getCode());
        self::assertSame('exception', $this->finishedSpans()[0]->getEvents()[0]->getName());
    }

    private static function value(string $value): Command
    {
        return new Command(RuntimeName::PHP(), CommandType::VALUE(), [$value]);
    }

    private static function fetch(): Command
    {
        return new Command(RuntimeName::PHP(), CommandType::INVOKE_STATIC_METHOD(), [
            new Command(RuntimeName::PHP(), CommandType::GET_TYPE(), [self::value('Orders\\Service')]),
            self::value('fetch'),
        ]);
    }
}
