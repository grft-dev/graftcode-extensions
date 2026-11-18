<?php

declare(strict_types=1);

namespace Graftcode\Telemetry\Datadog\Tests\Support;

use OpenTelemetry\SDK\Common\Future\CancellationInterface;
use OpenTelemetry\SDK\Common\Future\CompletedFuture;
use OpenTelemetry\SDK\Common\Future\FutureInterface;
use OpenTelemetry\SDK\Trace\ImmutableSpan;
use OpenTelemetry\SDK\Trace\SpanExporterInterface;

final class RecordingSpanExporter implements SpanExporterInterface
{
    /** @var list<ImmutableSpan> */
    private array $spans = [];

    public int $flushCount = 0;

    public function export(iterable $batch, ?CancellationInterface $cancellation = null): FutureInterface
    {
        foreach ($batch as $span) {
            $this->spans[] = $span;
        }

        return new CompletedFuture(true);
    }

    public function shutdown(?CancellationInterface $cancellation = null): bool
    {
        return true;
    }

    public function forceFlush(?CancellationInterface $cancellation = null): bool
    {
        $this->flushCount++;

        return true;
    }

    /**
     * @return list<ImmutableSpan>
     */
    public function getSpans(): array
    {
        return $this->spans;
    }
}
