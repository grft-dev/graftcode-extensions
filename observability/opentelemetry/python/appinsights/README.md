# GraftCode App Insights Connector (Python)

Configures Application Insights for an application, together with the Graft bridge spans, in one call. It is a port of the .NET (`GraftCode.Netcore.Telemetry.AppInsightsConnector`) and Java (`com.graftcode.telemetry.appinsights`) connectors.

```python
from graftcode_telemetry_appinsights import GraftCodeAppInsightsConnector

with GraftCodeAppInsightsConnector.start("MyService") as telemetry:
    span = telemetry.start_span("MyOperation")
    telemetry.logger.info("My operation started {Step}", "one")
    span.end()
```

The connector reads `APPLICATIONINSIGHTS_CONNECTION_STRING` and exports traces and logs with the Azure Monitor exporter (`azure-monitor-opentelemetry-exporter`). It also attaches `graftcode-instrumentation`, so Hypertube calls produce `Graftcode` spans and Hypertube log lines go to the same pipeline. You can configure an explicit connection string with a callback or an options object:

```python
from graftcode_telemetry_appinsights import GraftCodeAppInsightsConnector, GraftCodeAppInsightsConnectorOptions

def configure(options):
    options.service_name = "MyService"
    options.connection_string = settings["ApplicationInsights:ConnectionString"]

telemetry = GraftCodeAppInsightsConnector.start(configure)
telemetry = GraftCodeAppInsightsConnector.start(GraftCodeAppInsightsConnectorOptions(service_name="MyService"))
```

| Option | Default |
|--------|---------|
| `service_name` | required; becomes `service.name` (the App Insights cloud role) and the tracer/logger name |
| `connection_string` | falls back to `APPLICATIONINSIGHTS_CONNECTION_STRING` |
| `minimum_log_level` | `LogsLevel.Info`; applies to the connector logger and to forwarded Hypertube lines |
| `traces_per_second` | `None` (every trace is kept) |
| `enable_console_logging` | `True` |

## Pipeline (`GraftCodeAppInsightsConnectorPipeline`)

| Member | Purpose |
|--------|---------|
| `tracer`, `start_span(name, kind=INTERNAL, context=None)` | spans for the application |
| `logger` | `GraftCodeAppInsightsLogger` (`trace`, `debug`, `info`, `warning`, `error`, `log`) |
| `instrumentation` | the attached `GraftcodeInstrumentation` |
| `force_flush(timeout_millis=5000)` | flushes traces and logs |
| `close()` / context manager | flushes, detaches the instrumentation, and shuts the providers down; calling it twice is safe |
| `is_exporting_to_app_insights()` | `False` when no connection string was resolved |

The OpenTelemetry providers are not registered as globals, which matches the Java connector. Use `pipeline.tracer` or `pipeline.start_span` for application spans.

## Logger

The logger uses the same message templates as the .NET and Java connectors:

- `{Name}` placeholders are filled positionally. `{{` and `}}` escape braces, and format specifiers (`{Count:D3}`) are ignored. `None` renders as `(null)`.
- Each placeholder becomes a log attribute. The raw template is stored as `{OriginalFormat}`.
- Records are correlated with the current span.
- Console output follows the .NET console layout:

```
info: TestGgPythonWebApi[0]
      Caller after Graft call Result=fetched
```

To log an exception, pass it first: `logger.error(exception, "failed {Step}", "fetch")`. It is attached as `exception.type`, `exception.message` and `exception.stacktrace`.

Hypertube's own lines reach App Insights through the standard-library logger `Hypertube`. The pipeline sets that logger's level from `minimum_log_level` and adds a handler that emits through the OpenTelemetry logs API (instrumentation scope `Hypertube`).

## Request telemetry (ASGI)

Add the middleware to FastAPI or Starlette. It plays the role of `UseGraftCodeAppInsightsConnectorRequestTelemetry` in .NET and the servlet filter in Java:

```python
app.add_middleware(GraftCodeAppInsightsRequestTelemetryMiddleware, telemetry=telemetry)
```

Each HTTP request gets a `SERVER` span that is current while the endpoint runs:

- **Parent:** the incoming W3C `traceparent`, when present.
- **Attributes:** `http.method`, `http.target` and `http.status_code`. A status of 500 or higher marks the span as failed, and exceptions are recorded and re-raised. The path goes in `http.target` rather than `http.route` (as .NET and Java use) because the Python Azure Monitor exporter replaces the request name with `METHOD route` whenever `http.route` or a full URL is present; with `http.target` the request keeps the controller-based name, as in .NET and Java.
- **Name:** `{ServiceName}.{controller}` when the routed endpoint belongs to a class, for example a `FetchController.fetch` endpoint gives `TestGgPythonWebApi.Fetch`. An explicit name can be set in `scope["state"]["graft.controller"]`. Otherwise the name is `{ServiceName} {METHOD} {path}`.

Synchronous endpoints run in Starlette's thread pool with a copy of the context, so Graft calls made there share the request's trace.

## Sampling

Every trace is kept by default (`ALWAYS_ON`); the reason is the same as in .NET and Java. Set `traces_per_second` to opt into a token-bucket `RateLimitedSampler`.

## Flushing

The pipeline flushes on `close()`, at interpreter exit (`atexit`), and on unhandled exceptions; both `sys.excepthook` and `threading.excepthook` are chained. Short-lived processes should still call `force_flush()` before exiting.

## Diagnosing missing telemetry

When no connection string is resolved, `is_exporting_to_app_insights()` returns `False` and telemetry stays local. The connector then logs `Application Insights telemetry for {ServiceName} is disabled: no connection string was configured. ...`.

## Tests

```powershell
$env:PYTHONPATH = "<HYPERTUBE>\src\python;<graftcode-extensions>\observability\Instrumentation\python"
python -m pip install -e .[dev]
python -m pytest
```
