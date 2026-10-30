# graftcode-instrumentation (Node.js)

OpenTelemetry instrumentation for Graftcode (Hypertube) calls: client and server spans, W3C trace context propagation, and Hypertube log forwarding.

Hypertube exposes a **broker** (`HypertubeBroker.attach` / `detach`); nothing is monkey-patched. Enabling attaches a broker listener and a log listener; disabling removes them.

## Usage

Register like any other OpenTelemetry instrumentation:

```js
import { registerInstrumentations } from '@opentelemetry/instrumentation'
import { GraftcodeInstrumentation } from 'graftcode-instrumentation'

registerInstrumentations({ instrumentations: [new GraftcodeInstrumentation()] })
```

## Configuration (`GraftcodeInstrumentationConfig`)

| Option | Description |
|--------|-------------|
| `enabled` | Default `true`. |
| `captureBodies` | Record redacted request and response payloads on spans. Defaults to env `HYPERTUBE_TELEMETRY_CAPTURE_BODIES`, otherwise `false`. |
| `maxBodyLength` | Characters kept per payload, 256–32768. Defaults to `HYPERTUBE_TELEMETRY_BODY_MAX_CHARS`, otherwise `8192`. |
| `propagateContext` | Send W3C trace context with outgoing calls. Defaults to `HYPERTUBE_TELEMETRY_AUTO_INJECT`, otherwise `true`. |
| `logLevel` | Lowest Hypertube log level forwarded to OpenTelemetry logs (`trace`, `debug`, `info`, `warning`, `error`, `critical`, `off`). Default `info`. |
| `ignore` | `(operation) => boolean` — return `true` to skip tracing an operation. |
| `libraries` | Names the library behind a connection for the `Graft <library> <channel>` dependency name. Map from connection address (e.g. `ws://localhost:9180/ws`) to library name, or a function `(target) => string \| undefined`. Node.js types carry no namespace; without this, the caller falls back to `Graft <runtime> <channel>` unless the call loads the library. |

## Span model

- **Caller:** span name `Graft <library> [version] <channel>` (or runtime fallback), kind `INTERNAL`, attributes `graft.dependency.type` = `Graft`, `graft.dependency.target`, plus Hypertube operation metadata.
- **Callee:** span name `Service.method` (or `method` for globals), kind `INTERNAL`.
- Remote failures that return an `Exception` command mark the span failed from that response (not only thrown errors).

## Trace context

- Outgoing: W3C `traceparent` / `tracestate` via `callContext.outgoingHeaders`, plus `traceid` when sampled.
- Incoming: prefers W3C headers; a Graftcode `traceid` header alone still joins that trace under a synthetic remote parent.
- A `traceparent` already on the connection is not overridden.

## Library catalog

`recordLibraries` remembers libraries loaded on a connection (`LoadLibrary`) so later calls can be attributed. `describeTarget` formats `<library> [version] <channel>`. Version is read from local `package.json` when available. Package folder naming rules apply for `package.json`, `index.js`, `main.js`, etc.

## Body capture

When enabled, request/response bodies are JSON-like renderings with redaction and truncation (`hypertube.request.body`, `hypertube.response.body`, `.truncated` when clipped).

## Operations (`resolveOperation`)

Maps Hypertube commands to `GraftOperation`: `name` (`Service.method` or `method`), `service`, `method`, `commandType`. Skips non-member / control commands without a resolvable method name.

## Log bridge

Forwards Hypertube log records to the OpenTelemetry Logs API, correlated with the active span, respecting configured minimum level and redaction on errors.
