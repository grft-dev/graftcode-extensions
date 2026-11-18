<?php

declare(strict_types=1);

namespace Graftcode\Telemetry\Datadog;

use InvalidArgumentException;
use OpenTelemetry\SDK\Logs\LogRecordExporterInterface;
use OpenTelemetry\SDK\Trace\SpanExporterInterface;

final class GraftCodeDatadogConnector
{
    private static ?GraftCodeDatadogConnectorPipeline $active = null;

    private function __construct()
    {
    }

    /**
     * @param callable(GraftCodeDatadogConnectorOptions): void|null $configure
     */
    public static function start(
        ?string $serviceName = null,
        ?callable $configure = null,
        ?GraftCodeDatadogConnectorOptions $options = null,
        ?SpanExporterInterface $spanExporter = null,
        ?LogRecordExporterInterface $logRecordExporter = null
    ): GraftCodeDatadogConnectorPipeline {
        $options ??= new GraftCodeDatadogConnectorOptions();
        if ($serviceName !== null) {
            $options->serviceName = $serviceName;
        }

        if ($configure !== null) {
            $configure($options);
        }

        if (self::$active !== null) {
            if (self::$active->getServiceName() === trim($options->serviceName)) {
                return self::$active;
            }

            throw new InvalidArgumentException('GraftCode Datadog telemetry is already started for ' . self::$active->getServiceName());
        }

        return self::$active = new GraftCodeDatadogConnectorPipeline(
            $options,
            $spanExporter,
            $logRecordExporter,
            static function (GraftCodeDatadogConnectorPipeline $pipeline): void {
                if (self::$active === $pipeline) {
                    self::$active = null;
                }
            }
        );
    }

    public static function getActive(): ?GraftCodeDatadogConnectorPipeline
    {
        return self::$active;
    }
}
