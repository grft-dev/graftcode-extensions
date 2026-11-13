# graftcode-instrumentation (Ruby)

OpenTelemetry instrumentation for Graftcode (Hypertube) calls: client and server spans, W3C trace context propagation, and Hypertube log forwarding.

Hypertube exposes a **broker** (`Hypertube::Utils::Abstractions::HypertubeBroker.attach` / `detach`); nothing is monkey-patched. Attach registers broker and log listeners; `close` removes them.

## Requirements

- [`hypertube-ruby-sdk`](https://rubygems.org/gems/hypertube-ruby-sdk) from rubygems.org, in a release with the broker abstractions (3.1.19 and earlier do not have them yet)
- `opentelemetry-api` at runtime; configure a tracer provider with `opentelemetry-sdk` (or another exporter pipeline)

In a library hosted by Graftcode Gateway, the Hypertube SDK is loaded from the gateway's `Binaries/Ruby` folder rather than as a gem. The instrumentation then reuses that loaded copy instead of requiring `hypertube-ruby-sdk`, so the broker and logger it attaches to are the ones handling calls.

## Development

```bash
bundle install
bundle exec rspec
```

Dependencies are declared in `graftcode-instrumentation.gemspec`. `bundle install` resolves them from rubygems.org.

To test against an unreleased `hypertube-ruby-sdk`, publish it to a local gem repository, install that build, then re-lock:

```powershell
C:\dev\graft-packages\build-ruby.ps1
C:\dev\graft-packages\serve-ruby.ps1   # keep running; Bundler uses the compact index only over HTTP
gem install hypertube-ruby-sdk -v 3.1.18 --source http://127.0.0.1:8808
bundle lock --update hypertube-ruby-sdk
bundle install
```

The local build is stamped 3.1.18, the version generated grafts pin, which rubygems.org also has. Bundler reuses an installed gem of the same version from any source, so run `gem uninstall hypertube-ruby-sdk -v 3.1.18` before going back to rubygems.org.

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
  minimum_log_level: Hypertube::Utils::Logging::LogsLevel::INFO,
  logger_provider: logger_provider # optional
)

# ... Hypertube calls ...

listener.close
```

With a `logger_provider` (for example from `opentelemetry-logs-sdk`) and a `minimum_log_level`, Hypertube logs are exported as OpenTelemetry log records under the `Hypertube` scope, correlated with the current span. Without one, they are recorded as events on the current span.

## Configuration (environment)

| Variable | Default |
|----------|---------|
| `HYPERTUBE_TELEMETRY_AUTO_INJECT` | `true` |
| `HYPERTUBE_TELEMETRY_CAPTURE_BODIES` | `false` |
| `HYPERTUBE_TELEMETRY_BODY_MAX_CHARS` | `8192` (clamp 256–32768) |

## Span model

- **Caller:** `Graft <library> [version] <channel>` (or `<Runtime> <channel>`), attributes `graft.dependency.type` = `Graft`, `graft.dependency.target`, plus Hypertube metadata.
- **Callee:** `Service.method` (or global `method`), kind internal.
- A call that raises marks the span failed with `exception.type` and a redacted `exception.message`. A remote `Exception` response completes the span with status OK, as in .NET and Java.

## Trace context

Outgoing calls inject W3C `traceparent` / `tracestate` and `traceid` via thread-local connection headers when auto-inject is enabled. Only the W3C trace context is used, whatever `OpenTelemetry.propagation` is configured to, so no `baggage` header is sent. Incoming handling prefers W3C headers (matched case-insensitively); `traceid` alone joins an existing trace with a synthetic remote parent.
