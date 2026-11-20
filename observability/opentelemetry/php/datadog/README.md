# graftcode/telemetry-datadog (PHP)

Configure Datadog traces, logs, and Graft bridge spans for a PHP service with one call:

```php
use Graftcode\Telemetry\Datadog\GraftCodeDatadogConnector;

$telemetry = GraftCodeDatadogConnector::start('MyService');

$telemetry->inSpan('MyOperation', function () use ($telemetry, $input) {
    $telemetry->getLogger()->info('My operation started Input={Input}', $input);
});
```

The connector sends OTLP/HTTP (protobuf) to the Datadog Agent's OTLP receiver. It uses the public OpenTelemetry PHP SDK and exporters, not `datadog/dd-trace` (the `ddtrace` extension), which a library hosted by Graftcode Gateway cannot load: the gateway's embedded PHP runtime has no extensions. For the same reason the OTLP exporter sends through Guzzle, which falls back to PHP streams when `curl` is missing.

## Datadog Agent

Enable OTLP ingestion on the Agent (7.48 or later; logs need `DD_LOGS_ENABLED=true`):

```yaml
DD_OTLP_CONFIG_RECEIVER_PROTOCOLS_HTTP_ENDPOINT: "0.0.0.0:4318"
DD_OTLP_CONFIG_LOGS_ENABLED: "true"
```

Publish port `4318`. A containerized Agent 7.61 or later also needs `HOST_PROC: /proc`. Without it the OTLP pipeline fails with `failed to register process metrics`.

## Options (`GraftCodeDatadogConnectorOptions`)

```php
GraftCodeDatadogConnector::start('MyService', function (GraftCodeDatadogConnectorOptions $options): void {
    $options->environment = 'staging';
    $options->setMinimumLogLevel('debug');
});
```

| Option | Default |
|--------|---------|
| `serviceName` | Required; becomes the Datadog `service`. |
| `endpoint` | `OTEL_EXPORTER_OTLP_ENDPOINT`, then `http://127.0.0.1:4318`. |
| `environment` | `DD_ENV`; exported as `deployment.environment.name` (Datadog `env`). |
| `version` | `DD_VERSION`; exported as `service.version`. |
| `setMinimumLogLevel()` | `LogsLevel::INFO`. Applies to app logs and forwarded Hypertube logs. |
| `enableConsoleLogging` | `true`. Also writes app logs to stdout. |
| `flushAfterCalls` | Detected; see [Hosted libraries](#hosted-libraries). |

`start` is single-process. A second call with the same service name returns the running pipeline, and a different name throws `InvalidArgumentException`.

## Pipeline (`GraftCodeDatadogConnectorPipeline`)

| Member | Description |
|--------|-------------|
| `getTracer()` | OpenTelemetry tracer for the service. |
| `inSpan($name, $work, $kind, $attributes)` | Runs `$work` in a new current span. |
| `getLogger()` | `trace`, `debug`, `info`, `warning`/`warn`, `error`. Takes message templates (`{Name}` placeholders, filled by position) and a named `exception:` argument. Records carry the current trace and span IDs. |
| `flush()` | Exports buffered traces and logs; returns `true` on success. |
| `close()` / `shutdown()` | Flushes and stops; safe to call more than once and also runs at shutdown. |

The pipeline also registers the global tracer provider, logger provider, and W3C trace context propagator, then attaches `graftcode/instrumentation` with its logger provider. Hypertube's own logs (`Hypertube invoking/completed/handling/handled ...`) are therefore exported as log records at the minimum log level, in the same trace as the call.

## Request telemetry (PSR-15)

```php
// Slim 4: add it before the routing middleware, so it runs after routing.
$app->add(new GraftCodeDatadogRequestTelemetryMiddleware($telemetry));
$app->addRoutingMiddleware();
```

Each request gets a server span, continuing an incoming `traceparent`. The span is named `{service}.{controller}` when a controller is known, for example `TestGgPhpWebApi.Fetch`. The controller comes from the `graft.controller` request attribute or, in Slim, from the route's controller class without its `Controller` suffix. Without a controller the name is `{service} {METHOD} {route}`, where the route is the Slim route pattern (or the `graft.route` attribute) when present. The name is also set as `resource.name`, which Datadog uses as the resource. Responses with status 500 or higher, and thrown exceptions, mark the span as an error.

## Hosted libraries

PHP has no background threads, so the batch processors only export when a batch fills up or at shutdown, and a library hosted by Graftcode Gateway never reaches shutdown between calls. The connector detects a hosted runtime (its Hypertube receiver is loaded from the gateway's `Binaries/Php` folder) and then flushes on the handling side after every Graft call. Set `flushAfterCalls` to force this on or off.

The hosted library must not install `graftcode/hypertube-php-sdk` into its own `vendor/`; the gateway provides it.

## Development

```powershell
C:\dev\graft-packages\build-php.ps1           # publishes the local SDK and instrumentation builds
$env:COMPOSER_HOME = "C:\dev\graft-packages\composer-home"
composer update
vendor\bin\phpunit
```

See [instrumentation development](../../../Instrumentation/php/README.md#development) for the local Composer repository.
