<?php

declare(strict_types=1);

namespace Graftcode\Telemetry\Datadog;

use HypertubePhpUtils\abstractions\HypertubeCallContext;
use HypertubePhpUtils\abstractions\IHypertubeHandling;
use HypertubePhpUtils\CommandInterface;
use Throwable;

/**
 * PHP has no exporter threads, so a hosted runtime, which never reaches shutdown between calls,
 * exports its telemetry on the handling side after every Graft call.
 */
final class CallFlusher implements IHypertubeHandling
{
    private GraftCodeDatadogConnectorPipeline $pipeline;

    public function __construct(GraftCodeDatadogConnectorPipeline $pipeline)
    {
        $this->pipeline = $pipeline;
    }

    public function onProcess(HypertubeCallContext $context, CommandInterface $command): void
    {
    }

    public function onProcessed(HypertubeCallContext $context, CommandInterface $response): void
    {
        $this->pipeline->flush();
    }

    public function onHandlingError(HypertubeCallContext $context, Throwable $exception): void
    {
        $this->pipeline->flush();
    }
}
