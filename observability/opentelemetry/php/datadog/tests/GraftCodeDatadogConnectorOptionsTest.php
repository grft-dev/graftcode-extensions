<?php

declare(strict_types=1);

namespace Graftcode\Telemetry\Datadog\Tests;

use Graftcode\Telemetry\Datadog\GraftCodeDatadogConnectorOptions;
use HypertubePhpUtils\logging\LogsLevel;
use InvalidArgumentException;
use PHPUnit\Framework\TestCase;

final class GraftCodeDatadogConnectorOptionsTest extends TestCase
{
    public function testDefaultsToTheLocalAgentOtlpEndpoint(): void
    {
        self::assertSame('http://127.0.0.1:4318', (new GraftCodeDatadogConnectorOptions([]))->resolveEndpoint());
    }

    public function testReadsTheEndpointEnvironmentAndVersionFromTheEnvironment(): void
    {
        $options = new GraftCodeDatadogConnectorOptions([
            'OTEL_EXPORTER_OTLP_ENDPOINT' => 'http://agent:4318/',
            'DD_ENV' => 'staging',
            'DD_VERSION' => '2.0.0',
        ]);

        self::assertSame('http://agent:4318', $options->resolveEndpoint());
        self::assertSame('staging', $options->resolveEnvironment());
        self::assertSame('2.0.0', $options->resolveVersion());
    }

    public function testPrefersConfiguredValuesOverTheEnvironment(): void
    {
        $options = new GraftCodeDatadogConnectorOptions(['DD_ENV' => 'staging']);
        $options->environment = 'local';
        $options->endpoint = 'http://collector:4318';

        self::assertSame('local', $options->resolveEnvironment());
        self::assertSame('http://collector:4318', $options->resolveEndpoint());
        self::assertNull($options->resolveVersion());
    }

    public function testDefaultsTheMinimumLogLevelToInfoAndParsesNames(): void
    {
        $options = new GraftCodeDatadogConnectorOptions([]);
        self::assertSame(LogsLevel::INFO, $options->getMinimumLogLevel());

        $options->setMinimumLogLevel('debug');
        self::assertSame(LogsLevel::DEBUG, $options->getMinimumLogLevel());
    }

    public function testRejectsAnUnknownLogLevelName(): void
    {
        $options = new GraftCodeDatadogConnectorOptions([]);
        $options->serviceName = 'Orders';
        $options->setMinimumLogLevel('loud');

        $this->expectException(InvalidArgumentException::class);
        $options->validate();
    }

    public function testDetectsAHostedRuntimeFromItsReceiver(): void
    {
        $options = new GraftCodeDatadogConnectorOptions([]);

        self::assertTrue($options->resolveFlushAfterCalls([
            'C:\\app\\.graftcode\\1.4.1\\Binaries\\Php\\hypertube\\core\\receiver\\Receiver.php',
        ]));
        self::assertFalse($options->resolveFlushAfterCalls(['C:\\app\\vendor\\graftcode\\hypertube-php-sdk\\hypertube\\core\\receiver\\Receiver.php']));

        $options->flushAfterCalls = false;
        self::assertFalse($options->resolveFlushAfterCalls([
            'C:\\app\\.graftcode\\1.4.1\\Binaries\\Php\\hypertube\\core\\receiver\\Receiver.php',
        ]));
    }
}
