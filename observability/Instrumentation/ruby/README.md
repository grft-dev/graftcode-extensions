# graftcode-instrumentation (Ruby)

OpenTelemetry instrumentation for Graftcode (Hypertube) calls: client and server spans, W3C trace context propagation, and Hypertube log forwarding.

Hypertube exposes a **broker** (`Hypertube::Utils::Abstractions::HypertubeBroker.attach` / `detach`); nothing is monkey-patched. Attach registers broker and log listeners; `close` removes them.

## Requirements

- [`hypertube-ruby-sdk`](../../../../HYPERTUBE/src/ruby) with broker abstractions (Hypertube 3.1.18+)
- `opentelemetry-api` at runtime; configure a tracer provider with `opentelemetry-sdk` (or another exporter pipeline)

## Development

From this directory (use a local bundle path so Hypertube’s `.bundle/config` does not override it):

```bash
bundle install
bundle exec rspec
```

## Usage

```ruby
require 'opentelemetry/sdk'
require 'graftcode/instrumentation'

OpenTelemetry::SDK.configure do |c|
  c.add_span_processor(
    OpenTelemetry::SDK::Trace::Export::BatchSpanProcessor.new(your_exporter)
  )
end

listener = Graftcode::Instrumentation.attach(
  OpenTelemetry.tracer_provider,
  minimum_log_level: Hypertube::Utils::Logging::LogsLevel::INFO
)

# ... Hypertube calls ...

listener.close
```

## Configuration (environment)

| Variable | Default |
|----------|---------|
| `HYPERTUBE_TELEMETRY_AUTO_INJECT` | `true` |
| `HYPERTUBE_TELEMETRY_CAPTURE_BODIES` | `false` |
| `HYPERTUBE_TELEMETRY_BODY_MAX_CHARS` | `8192` (clamp 256–32768) |

## Span model

- **Caller:** `Graft <library> [version] <channel>` (or `<Runtime> <channel>`), attributes `graft.dependency.type` = `Graft`, `graft.dependency.target`, plus Hypertube metadata.
- **Callee:** `Service.method` (or global `method`), kind internal.
- Remote `Exception` command responses mark the span failed (not only raised errors).

## Trace context

Outgoing calls inject W3C `traceparent` / `tracestate` and `traceid` via thread-local connection headers when auto-inject is enabled. Incoming handling prefers W3C headers; `traceid` alone joins an existing trace with a synthetic remote parent.
