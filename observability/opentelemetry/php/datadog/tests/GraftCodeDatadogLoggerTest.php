<?php

declare(strict_types=1);

namespace Graftcode\Telemetry\Datadog\Tests;

use Graftcode\Telemetry\Datadog\GraftCodeDatadogLogger;
use HypertubePhpUtils\logging\LogsLevel;
use OpenTelemetry\SDK\Logs\Exporter\InMemoryExporter;
use OpenTelemetry\SDK\Logs\LoggerProvider;
use OpenTelemetry\SDK\Logs\Processor\SimpleLogRecordProcessor;
use OpenTelemetry\SDK\Logs\ReadableLogRecord;
use PHPUnit\Framework\TestCase;
use RuntimeException;

final class GraftCodeDatadogLoggerTest extends TestCase
{
    private InMemoryExporter $exporter;
    /** @var resource */
    private $console;
    private GraftCodeDatadogLogger $logger;

    protected function setUp(): void
    {
        $this->exporter = new InMemoryExporter();
        $provider = LoggerProvider::builder()->addLogRecordProcessor(new SimpleLogRecordProcessor($this->exporter))->build();
        $this->console = fopen('php://memory', 'w+b');
        $this->logger = new GraftCodeDatadogLogger('TestGgPhpLib', $provider->getLogger('TestGgPhpLib'), LogsLevel::INFO, $this->console);
    }

    public function testEmitsARecordWithTheRenderedMessageAndTemplate(): void
    {
        $this->logger->info('Business logic starting Input={Input}', 'abc');

        $record = $this->single();
        self::assertSame('Business logic starting Input=abc', $record->getBody());
        self::assertSame(9, $record->getSeverityNumber());
        self::assertSame('info', $record->getSeverityText());
        self::assertSame('abc', $record->getAttributes()->get('Input'));
        self::assertSame('Business logic starting Input={Input}', $record->getAttributes()->get('{OriginalFormat}'));
    }

    public function testWritesConsoleLinesInTheDotnetFormat(): void
    {
        $this->logger->warning('Slow call {Elapsed}', 42);

        rewind($this->console);
        self::assertSame("warn: TestGgPhpLib[0]\n      Slow call 42\n", stream_get_contents($this->console));
    }

    public function testRecordsAnExceptionPassedByName(): void
    {
        $this->logger->error('Call failed {Operation}', 'fetch', exception: new RuntimeException('boom'));

        $attributes = $this->single()->getAttributes();
        self::assertSame('fetch', $attributes->get('Operation'));
        self::assertSame(RuntimeException::class, $attributes->get('exception.type'));
        self::assertSame('boom', $attributes->get('exception.message'));
        self::assertSame(17, $this->single()->getSeverityNumber());
    }

    public function testSkipsLevelsBelowTheMinimum(): void
    {
        $this->logger->debug('hidden');

        self::assertSame([], $this->exporter->getStorage()->getArrayCopy());
        self::assertFalse($this->logger->isEnabled(LogsLevel::OFF));
    }

    private function single(): ReadableLogRecord
    {
        $records = array_values($this->exporter->getStorage()->getArrayCopy());
        self::assertCount(1, $records);

        return $records[0];
    }
}
