<?php

declare(strict_types=1);

namespace Graftcode\Telemetry\Datadog\Tests\Support;

use Graftcode\Telemetry\Datadog\GraftCodeDatadogConnector;
use Graftcode\Telemetry\Datadog\GraftCodeDatadogConnectorOptions;
use Graftcode\Telemetry\Datadog\GraftCodeDatadogConnectorPipeline;
use HypertubePhpUtils\logging\HypertubeLogger;
use HypertubePhpUtils\logging\LogsLevel;
use OpenTelemetry\SDK\Logs\Exporter\InMemoryExporter as InMemoryLogExporter;
use OpenTelemetry\SDK\Logs\ReadableLogRecord;
use OpenTelemetry\SDK\Trace\ImmutableSpan;
use PHPUnit\Framework\TestCase;

abstract class TelemetryTestCase extends TestCase
{
    protected RecordingSpanExporter $spans;
    protected InMemoryLogExporter $logs;
    private int $savedLevel;

    protected function setUp(): void
    {
        $this->spans = new RecordingSpanExporter();
        $this->logs = new InMemoryLogExporter();
        $this->savedLevel = HypertubeLogger::getMinimumLevel();
        HypertubeLogger::setMinimumLevel(LogsLevel::OFF);
    }

    protected function tearDown(): void
    {
        GraftCodeDatadogConnector::getActive()?->close();
        HypertubeLogger::setMinimumLevel($this->savedLevel);
    }

    /**
     * @param array<string, mixed> $settings options properties to set
     */
    protected function startTelemetry(string $serviceName = 'Orders', array $settings = []): GraftCodeDatadogConnectorPipeline
    {
        $options = new GraftCodeDatadogConnectorOptions([]);
        $options->enableConsoleLogging = false;
        $options->flushAfterCalls = false;
        foreach ($settings as $name => $value) {
            $options->$name = $value;
        }

        return GraftCodeDatadogConnector::start($serviceName, null, $options, $this->spans, $this->logs);
    }

    /**
     * @return list<ImmutableSpan>
     */
    protected function finishedSpans(): array
    {
        return $this->spans->getSpans();
    }

    /**
     * @return list<ReadableLogRecord>
     */
    protected function emittedLogs(): array
    {
        return array_values($this->logs->getStorage()->getArrayCopy());
    }
}
