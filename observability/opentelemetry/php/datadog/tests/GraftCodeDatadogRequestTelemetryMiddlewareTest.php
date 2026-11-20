<?php

declare(strict_types=1);

namespace Graftcode\Telemetry\Datadog\Tests;

use Graftcode\Telemetry\Datadog\GraftCodeDatadogRequestTelemetryMiddleware;
use Graftcode\Telemetry\Datadog\Tests\Support\TelemetryTestCase;
use InvalidArgumentException;
use Nyholm\Psr7\Response;
use Nyholm\Psr7\ServerRequest;
use OpenTelemetry\API\Trace\Span;
use OpenTelemetry\API\Trace\SpanKind;
use Psr\Http\Message\ResponseInterface;
use Psr\Http\Message\ServerRequestInterface;
use Psr\Http\Server\RequestHandlerInterface;

final class GraftCodeDatadogRequestTelemetryMiddlewareTest extends TelemetryTestCase
{
    public function testOpensAServerSpanNamedAfterTheController(): void
    {
        $request = (new ServerRequest('GET', '/api/fetch/abc'))
            ->withAttribute('__route__', new FakeRoute('/api/fetch/{input}', ['TestGgPhpWebApi\\FetchController', 'fetch']));

        $this->request($request, static fn () => new Response(200));

        $span = $this->finishedSpans()[0];
        self::assertSame('TestGgPhpWebApi.Fetch', $span->getName());
        self::assertSame(SpanKind::KIND_SERVER, $span->getKind());
        $attributes = $span->getAttributes();
        self::assertSame('GET', $attributes->get('http.method'));
        self::assertSame('/api/fetch/{input}', $attributes->get('http.route'));
        self::assertSame(200, $attributes->get('http.status_code'));
        self::assertSame('TestGgPhpWebApi.Fetch', $attributes->get('resource.name'));
    }

    public function testUsesAnExplicitControllerAttribute(): void
    {
        $this->request((new ServerRequest('GET', '/x'))->withAttribute('graft.controller', 'Fetch'), static fn () => new Response(200));

        self::assertSame('TestGgPhpWebApi.Fetch', $this->finishedSpans()[0]->getName());
    }

    public function testFallsBackToTheMethodAndPath(): void
    {
        $this->request(new ServerRequest('GET', '/health'), static fn () => new Response(204));

        self::assertSame('TestGgPhpWebApi GET /health', $this->finishedSpans()[0]->getName());
    }

    public function testContinuesTheIncomingW3CTraceAndMakesTheSpanCurrentForTheApp(): void
    {
        $traceId = '0af7651916cd43dd8448eb211c80319c';
        $current = null;
        $request = new ServerRequest('GET', '/api', ['traceparent' => "00-{$traceId}-b7ad6b7169203331-01"]);

        $this->request($request, static function () use (&$current) {
            $current = Span::getCurrent()->getContext()->getSpanId();

            return new Response(200);
        });

        $span = $this->finishedSpans()[0];
        self::assertSame($traceId, $span->getTraceId());
        self::assertSame('b7ad6b7169203331', $span->getParentSpanId());
        self::assertSame($span->getSpanId(), $current);
    }

    public function testMarksServerErrorsAndRecordsThrownExceptions(): void
    {
        try {
            $this->request(new ServerRequest('GET', '/boom'), static function (): ResponseInterface {
                throw new InvalidArgumentException('bad input');
            });
            self::fail('The exception was swallowed.');
        } catch (InvalidArgumentException $exception) {
            self::assertSame('bad input', $exception->getMessage());
        }

        $span = $this->finishedSpans()[0];
        self::assertSame('Error', $span->getStatus()->getCode());
        self::assertSame('exception', $span->getEvents()[0]->getName());

        $this->request(new ServerRequest('GET', '/fail'), static fn () => new Response(503));
        self::assertSame('Error', $this->finishedSpans()[1]->getStatus()->getCode());
    }

    /**
     * @param callable(ServerRequestInterface): ResponseInterface $app
     */
    private function request(ServerRequestInterface $request, callable $app): ResponseInterface
    {
        $middleware = new GraftCodeDatadogRequestTelemetryMiddleware($this->startTelemetry('TestGgPhpWebApi'));

        return $middleware->process($request, new class ($app) implements RequestHandlerInterface {
            /** @var callable */
            private $app;

            public function __construct(callable $app)
            {
                $this->app = $app;
            }

            public function handle(ServerRequestInterface $request): ResponseInterface
            {
                return ($this->app)($request);
            }
        });
    }
}

final class FakeRoute
{
    /**
     * @param mixed $callable
     */
    public function __construct(private string $pattern, private $callable)
    {
    }

    public function getPattern(): string
    {
        return $this->pattern;
    }

    /**
     * @return mixed
     */
    public function getCallable()
    {
        return $this->callable;
    }
}
