# graftcode-telemetry-datadog (Ruby)

Configure Datadog traces, logs, and Graft bridge spans for a Ruby service with one call:

```ruby
require 'graftcode/telemetry/datadog'

telemetry = Graftcode::Telemetry::Datadog::GraftCodeDatadogConnector.start('MyService')

telemetry.in_span('MyOperation') do
  telemetry.logger.info('My operation started Input={Input}', input)
end
```

The connector sends OTLP/HTTP to the Datadog Agent's OTLP receiver. It uses the public OpenTelemetry Ruby SDK and exporters, not `datadog` (dd-trace-rb). dd-trace-rb does not support Windows and does not export logs, and Graftcode-hosted Ruby libraries run on Windows.

## Datadog Agent

Enable OTLP ingestion on the Agent (7.48 or later; logs need `DD_LOGS_ENABLED=true`):

```yaml
DD_OTLP_CONFIG_RECEIVER_PROTOCOLS_HTTP_ENDPOINT: "0.0.0.0:4318"
DD_OTLP_CONFIG_LOGS_ENABLED: "true"
```

Publish port `4318`. A containerized Agent 7.61 or later also needs `HOST_PROC: /proc`. Without it the OTLP pipeline fails with `failed to register process metrics`.

## Options (`GraftCodeDatadogConnectorOptions`)

```ruby
GraftCodeDatadogConnector.start('MyService') do |options|
  options.environment = 'staging'
  options.minimum_log_level = 'debug'
end
```

| Option | Default |
|--------|---------|
| `service_name` | Required; becomes the Datadog `service`. |
| `endpoint` | `OTEL_EXPORTER_OTLP_ENDPOINT`, then `http://127.0.0.1:4318`. |
| `environment` | `DD_ENV`; exported as `deployment.environment.name` (Datadog `env`). |
| `version` | `DD_VERSION`; exported as `service.version`. |
| `minimum_log_level` | `info`. Applies to app logs and forwarded Hypertube logs. |
| `enable_console_logging` | `true`. Also writes app logs to stdout. |
| `flush_after_calls` | Detected; see [Hosted libraries](#hosted-libraries). |

`start` is single-process. A second call with the same service name returns the running pipeline, and a different name raises `ArgumentError`.

## Pipeline (`GraftCodeDatadogConnectorPipeline`)

| Member | Description |
|--------|-------------|
| `tracer` | OpenTelemetry tracer for the service. |
| `in_span(name, kind:, attributes:)` | Runs a block in a new current span. |
| `logger` | `trace`, `debug`, `info`, `warning`/`warn`, `error`. Takes message templates (`{Name}` placeholders, filled by position) and `exception:`. Records carry the current trace and span IDs. |
| `flush(timeout = 5)` | Exports buffered traces and logs; returns `true` on success. |
| `close` / `shutdown` | Flushes and stops; safe to call more than once and also runs at exit. |

The pipeline also sets the global tracer provider, logger provider, and W3C trace context propagator, then attaches `graftcode-instrumentation` with its logger provider. Hypertube's own logs (`Hypertube invoking/completed/handling/handled ...`) are therefore exported as log records at `minimum_log_level`, in the same trace as the call.

## Rack request telemetry

```ruby
# config.ru
use Graftcode::Telemetry::Datadog::GraftCodeDatadogRequestTelemetryMiddleware, telemetry: Telemetry.pipeline
run MyApp
```

Each request gets a server span, continuing an incoming `traceparent`. The span is named `{service}.{controller}` when a controller is known, for example `TestGgRubyWebApi.Fetch`. The controller comes from `env['graft.controller']` or, in Rails, from the controller class without its `Controller` suffix. Without a controller the name is `{service} {METHOD} {route}`, where the route is taken from Sinatra's route template when present. The name is also set as `resource.name`, which Datadog uses as the resource. Responses with status 500 or higher, and raised exceptions, mark the span as an error.

## Hosted libraries

The Hypertube native launcher keeps the Ruby GVL while it waits for the next command. In a library hosted by Graftcode Gateway, the batch exporters' background threads therefore never run, and telemetry would only be sent at exit. The connector detects a hosted runtime (its Hypertube receiver is loaded from the gateway's `Binaries/Ruby` folder) and then flushes on the handling thread after every Graft call. Set `flush_after_calls` to force this on or off.

## Development

```powershell
bundle install
bundle exec rspec
```

`graftcode-instrumentation` is used from this repository and `hypertube-ruby-sdk` from rubygems.org. `HYPERTUBE_GEM_SOURCE` selects an unreleased Hypertube build, as described for the [instrumentation](../../../Instrumentation/ruby/README.md#development).
