<?php

declare(strict_types=1);

namespace Graftcode\Telemetry\Datadog;

use OpenTelemetry\API\Trace\Propagation\TraceContextPropagator;
use OpenTelemetry\API\Trace\SpanInterface;
use OpenTelemetry\API\Trace\SpanKind;
use OpenTelemetry\API\Trace\StatusCode;
use OpenTelemetry\Context\Context;
use Psr\Http\Message\ResponseInterface;
use Psr\Http\Message\ServerRequestInterface;
use Psr\Http\Server\MiddlewareInterface;
use Psr\Http\Server\RequestHandlerInterface;
use Throwable;

/**
 * PSR-15 middleware that opens a server span per request. Register it inside the router
 * (in Slim, add it before addRoutingMiddleware) so the route template and controller are known.
 */
final class GraftCodeDatadogRequestTelemetryMiddleware implements MiddlewareInterface
{
    public const CONTROLLER_KEY = 'graft.controller';
    public const ROUTE_KEY = 'graft.route';
    public const CONTROLLER_SUFFIX = 'Controller';
    public const SLIM_ROUTE_KEY = '__route__';
    public const RESOURCE_NAME = 'resource.name';

    private GraftCodeDatadogConnectorPipeline $telemetry;

    public function __construct(GraftCodeDatadogConnectorPipeline $telemetry)
    {
        $this->telemetry = $telemetry;
    }

    public function process(ServerRequestInterface $request, RequestHandlerInterface $handler): ResponseInterface
    {
        $method = $request->getMethod();
        $path = $request->getUri()->getPath() === '' ? '/' : $request->getUri()->getPath();
        $route = self::routeTemplate($request) ?? $path;
        $name = self::operationName($this->telemetry->getServiceName(), $method, $route, self::controllerName($request));

        $span = $this->telemetry->getTracer()->spanBuilder($name)
            ->setParent(TraceContextPropagator::getInstance()->extract(self::headers($request), null, Context::getRoot()))
            ->setSpanKind(SpanKind::KIND_SERVER)
            ->setAttribute('http.method', $method)
            ->setAttribute('http.route', $route)
            ->startSpan();
        $scope = $span->activate();

        try {
            $response = $handler->handle($request);
            self::applyResponseStatus($span, $response->getStatusCode());

            return $response;
        } catch (Throwable $exception) {
            $span->recordException($exception);
            $span->setStatus(StatusCode::STATUS_ERROR, $exception->getMessage());

            throw $exception;
        } finally {
            $span->setAttribute(self::RESOURCE_NAME, $name);
            $scope->detach();
            $span->end();
        }
    }

    public static function operationName(string $serviceName, string $method, ?string $path, ?string $controller): string
    {
        if ($controller !== null && trim($controller) !== '') {
            return $serviceName . '.' . $controller;
        }

        return $serviceName . ' ' . $method . ' ' . ($path === null || $path === '' ? '/' : $path);
    }

    /**
     * The controller comes from the `graft.controller` request attribute or, in Slim,
     * from the route's controller class without its `Controller` suffix.
     */
    public static function controllerName(ServerRequestInterface $request): ?string
    {
        $explicit = $request->getAttribute(self::CONTROLLER_KEY);
        if (is_string($explicit) && trim($explicit) !== '') {
            return $explicit;
        }

        $route = $request->getAttribute(self::SLIM_ROUTE_KEY);
        if (!is_object($route) || !method_exists($route, 'getCallable')) {
            return null;
        }

        $class = self::controllerClass($route->getCallable());

        return $class === null ? null : self::stripControllerSuffix(self::shortClassName($class));
    }

    /**
     * @param mixed $callable
     */
    private static function controllerClass($callable): ?string
    {
        if (is_array($callable) && isset($callable[0])) {
            return is_object($callable[0]) ? get_class($callable[0]) : (string) $callable[0];
        }

        if (is_string($callable) && preg_match('/^([^:]+)::?[^:]+$/', $callable, $match) === 1) {
            return $match[1];
        }

        return is_object($callable) && !$callable instanceof \Closure ? get_class($callable) : null;
    }

    private static function shortClassName(string $class): string
    {
        $separator = strrpos($class, '\\');

        return $separator === false ? $class : substr($class, $separator + 1);
    }

    private static function stripControllerSuffix(string $name): string
    {
        $suffixLength = strlen(self::CONTROLLER_SUFFIX);

        return strlen($name) > $suffixLength && str_ends_with($name, self::CONTROLLER_SUFFIX)
            ? substr($name, 0, -$suffixLength)
            : $name;
    }

    private static function routeTemplate(ServerRequestInterface $request): ?string
    {
        $explicit = $request->getAttribute(self::ROUTE_KEY);
        if (is_string($explicit) && $explicit !== '') {
            return $explicit;
        }

        $route = $request->getAttribute(self::SLIM_ROUTE_KEY);

        return is_object($route) && method_exists($route, 'getPattern') ? (string) $route->getPattern() : null;
    }

    /**
     * @return array<string, string>
     */
    private static function headers(ServerRequestInterface $request): array
    {
        $headers = [];
        foreach ($request->getHeaders() as $name => $values) {
            $headers[strtolower((string) $name)] = implode(',', $values);
        }

        return $headers;
    }

    private static function applyResponseStatus(SpanInterface $span, int $status): void
    {
        $span->setAttribute('http.status_code', $status);
        if ($status >= 500) {
            $span->setStatus(StatusCode::STATUS_ERROR);
        }
    }
}
