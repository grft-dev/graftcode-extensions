# graftcode/instrumentation (PHP)

OpenTelemetry instrumentation for Graftcode (Hypertube) calls: client and server spans, W3C trace context propagation, and Hypertube log forwarding.

Hypertube exposes a **broker** (`HypertubePhpUtils\abstractions\HypertubeBroker::attach` / `detach`); nothing is monkey-patched. `GraftcodeInstrumentation::attach` registers broker and log listeners; `dispose` removes them.

## Requirements

- PHP 8.2 or later
- [`graftcode/hypertube-php-sdk`](https://packagist.org/packages/graftcode/hypertube-php-sdk) in a release with the broker abstractions (`hypertube/utils/abstractions`)
- `open-telemetry/api` at runtime; configure a tracer provider with `open-telemetry/sdk` (or another exporter pipeline)

The SDK is not a hard dependency. In a library hosted by Graftcode Gateway, it is loaded from the gateway's `Binaries/Php` folder, and a second copy from `vendor/` would shadow the broker and logger that handle the calls. A caller application requires `graftcode/hypertube-php-sdk` itself (generated grafts already do).

## Development

```powershell
C:\dev\graft-packages\build-php.ps1           # publishes the local hypertube-php-sdk build
$env:COMPOSER_HOME = "C:\dev\graft-packages\composer-home"
composer update
vendor\bin\phpunit
```

`C:\dev\graft-packages\composer-home\config.json` adds the local Composer artifact repository (`C:\dev\graft-packages\composer`), so an unreleased `hypertube-php-sdk` resolves without changing `composer.json`. The local build is stamped 3.2.0, the version generated grafts pin.

## Usage

```php
use Graftcode\Instrumentation\GraftcodeInstrumentation;
use HypertubePhpUtils\logging\LogsLevel;

$listener = GraftcodeInstrumentation::attach(
    $tracerProvider,
    LogsLevel::INFO,
    $loggerProvider // optional
);

// ... Hypertube calls ...

$listener->dispose();
```

With a logger provider and a minimum log level, Hypertube logs are exported as OpenTelemetry log records under the `Hypertube` scope, correlated with the current span. Without one, they are recorded as events on the current span.

## Configuration (environment)

| Variable | Default |
|----------|---------|
| `HYPERTUBE_TELEMETRY_AUTO_INJECT` | `true` |
| `HYPERTUBE_TELEMETRY_CAPTURE_BODIES` | `false` |
| `HYPERTUBE_TELEMETRY_BODY_MAX_CHARS` | `8192` (clamp 256–32768) |

## Span model

- **Caller:** `Graft <library> [version] <channel>` (or `<Runtime> <channel>`), attributes `graft.dependency.type` = `Graft`, `graft.dependency.target`, plus Hypertube metadata (`hypertube.runtime`, `hypertube.command_type`, `hypertube.connection_type` use the .NET names, e.g. `Php`, `InvokeStaticMethod`, `WebSocket`). The library version comes from the `composer.json` next to the loaded library.
- **Callee:** `Namespace.Type.method` (PHP namespaces are reported with dots, e.g. `TestGgPhpLib.RandomService.fetch`), kind internal.
- A call that throws marks the span failed with `exception.type` and a redacted `exception.message`. A remote `Exception` response completes the span with status OK, as in .NET and Java.

## Trace context

Outgoing calls inject W3C `traceparent` / `tracestate` and `traceid` via thread-local connection headers when auto-inject is enabled. Only the W3C trace context is used, whatever propagator is configured globally, so no `baggage` header is sent. Incoming handling prefers W3C headers (matched case-insensitively); `traceid` alone joins an existing trace with a synthetic remote parent.

Over WebSocket, connection headers travel in the handshake, so as in .NET each distinct `traceparent` opens its own connection.
