# graftcode-datadog-connector (Ruby)

One-call Datadog setup for Ruby services that use Graftcode: OpenTelemetry traces exported over OTLP, application logging, and Hypertube `graftcode-instrumentation`.

## Why OpenTelemetry + OTLP instead of the Datadog Ruby tracer (`ddtrace`)?

`graftcode-instrumentation` and the other Graftcode observability pieces are built on the **OpenTelemetry API** (spans, W3C propagation). Datadog [accepts OTLP](https://docs.datadoghq.com/opentelemetry/) from the official OTLP exporter, so one stack stays aligned with .NET/Java/Node connectors and avoids translating between two tracing models. The **Datadog Ruby tracer** (`ddtrace` / `datadog`) is a separate product: strong auto-instrumentation for Rails and common libraries, but it does not replace OTel as the API for Graft spans; using both in one process is possible but means two tracers unless you add bridging. This connector intentionally standardizes on **OTLP → Datadog**.

## Quick start

Load as early as possible in the process:

```ruby
require 'graftcode/telemetry/datadog'

telemetry = Graftcode::Telemetry::Datadog.use(
  service_name: 'MyService',
  log_level: 'info'
)

telemetry.logger.info('Service started')
# ... Hypertube / Graftcode calls ...

telemetry.shutdown
```

## Export configuration

The connector sends traces to Datadog using **OTLP**. Configure one of:

| Source | Effect |
|--------|--------|
| `OTEL_EXPORTER_OTLP_ENDPOINT` / `OTEL_EXPORTER_OTLP_TRACES_ENDPOINT` | OTLP HTTP endpoint ( `/v1/traces` appended when missing ) |
| `DD_AGENT_HOST` | `http://<host>:4318/v1/traces` (Datadog Agent) |
| `DD_API_KEY` (+ optional `DD_SITE`) | Datadog OTLP intake |
| Options `otlp_endpoint`, `agent_host`, `api_key` | Same as above, passed explicitly |

Resource attributes: `service.name`, optional `deployment.environment` (`DD_ENV`), `service.version` (`DD_VERSION`).

Without export configuration, a **local** OpenTelemetry SDK is started (real trace IDs for Graft propagation) and a warning is logged once — same idea as the Application Insights connectors.

## Options

| Option | Description |
|--------|-------------|
| `service_name` | Required. Datadog / OTel service name. |
| `otlp_endpoint` | OTLP traces URL. |
| `agent_host` | Datadog Agent host when not using env vars. |
| `api_key` | Datadog API key for direct OTLP intake. |
| `site` | Datadog site (default `datadoghq.com`). |
| `environment` | `deployment.environment` resource attribute. |
| `version` | `service.version` resource attribute. |
| `traces_per_second` | Rate-limited sampling (default: keep all traces). |
| `log_level` | Minimum app + Hypertube log level (`info`, `warning`, …). |
| `console` | Mirror app logs to stdout (default `true`). |
| `graftcode` | `false` to skip `graftcode-instrumentation`. |

## Rack

```ruby
use Graftcode::Telemetry::Datadog::RackMiddleware, telemetry
```

Creates a server span per request and extracts W3C trace context from incoming headers.

## Return value

| Member | Description |
|--------|-------------|
| `service_name` | Configured service name. |
| `tracer` | OpenTelemetry tracer. |
| `logger` | App logger (`debug`, `info`, `warn`, `error`). |
| `exporting` | `true` when OTLP export is configured. |
| `flush` | Flush tracer provider. |
| `shutdown` | Flush, detach Hypertube instrumentation, shutdown SDK. |

`Graftcode::Telemetry::Datadog.use` is single-process: same `service_name` returns the existing handle; a different name raises.

## Development

```bash
cd observability/opentelemetry/ruby/datadog
bundle install
bundle exec rspec
```

Depends on local `graftcode-instrumentation`, `hypertube-ruby-sdk`, and OpenTelemetry SDK gems (see `Gemfile`).
