# graftcode-appinsights-connector (Node.js)

One-call Azure Application Insights setup for Node.js services that use Graftcode: traces, logs, and Graftcode call instrumentation.

## Quick start

Call once, as early as possible. For a web server, load before the HTTP framework (e.g. `node --import ./telemetry.js server.js`):

```js
import { useGraftCodeAppInsights } from 'graftcode-appinsights-connector'

export const telemetry = useGraftCodeAppInsights({
  serviceName: 'MyService',
  graftcode: {
    libraries: { 'ws://localhost:9180/ws': 'MyNodeLib' },
  },
})
```

## Options (`GraftCodeAppInsightsOptions`)

| Option | Description |
|--------|-------------|
| `serviceName` | Cloud role name in Application Insights (required). |
| `connectionString` | Defaults to `APPLICATIONINSIGHTS_CONNECTION_STRING`. |
| `tracesPerSecond` | Rate-limit sampling; default keeps every trace (`0`). |
| `samplingRatio` | Fixed ratio 0–1 when `tracesPerSecond` is not used; default `1`. |
| `logLevel` | Lowest level for app and Graftcode logs: `debug`, `info`, `warning`, `error`. Default `info`. |
| `console` | Also print app logs to stdout. Default `true`. |
| `graftcode` | `GraftcodeInstrumentation` options, or `false` to omit Graftcode instrumentation. |
| `instrumentations` | Additional OpenTelemetry instrumentations to register. |
| `azureMonitor` | Advanced `@azure/monitor-opentelemetry` options (merged first). |

## Return value (`GraftCodeTelemetry`)

| Member | Description |
|--------|-------------|
| `serviceName` | Configured role name. |
| `isExporting` | `true` when a connection string is configured. |
| `tracer` | OpenTelemetry tracer for the service. |
| `logger` | App logger (`debug`, `info`, `warn`, `error`) exported to OpenTelemetry logs. |
| `flush()` | Sends buffered telemetry now. |
| `shutdown()` | Flush and stop; safe to call more than once. |

`useGraftCodeAppInsights` is single-process: a second call with the same `serviceName` returns the existing instance; a different name throws.

## Without a connection string

Telemetry is not exported to Azure, but a local `NodeTracerProvider` still assigns real trace IDs so context flows through Graftcode calls. A warning is logged once.

## HTTP auto-instrumentation

Azure Monitor HTTP instrumentation stays enabled. Outgoing requests that are WebSocket upgrades (`Upgrade: websocket`) are ignored so the long-lived handshake is not tied to a single HTTP request; each Graft call is already described by the Graftcode span.

## App logger

Application logs use OpenTelemetry Logs with severity mapping and optional console output (`label: serviceName: message`). Errors attach redacted exception attributes.
