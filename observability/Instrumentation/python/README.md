# graftcode-instrumentation (Python)

OpenTelemetry instrumentation for Graftcode (Hypertube) calls in Python. It produces a caller span and a callee span per call, propagates W3C trace context, and forwards Hypertube log lines. It is a port of the .NET (`Graftcode.Instrumentation`) and Java (`com.graftcode.instrumentation`) packages. Class names, span names, attribute keys and header names are the same in all three.

Hypertube exposes a broker (`HypertubeBroker`, in `hypertube.utils.abstractions`) and a log listener registry (`HypertubeLogger`, in `hypertube.utils.logging`). Nothing is monkey-patched. `GraftcodeInstrumentation.attach(...)` registers one object as an invocation listener, a handling listener and a log listener; `close()` removes all three. The Hypertube SDK itself has no OpenTelemetry dependency.

## Usage

```python
import logging
from opentelemetry.sdk.trace import TracerProvider
from graftcode_instrumentation import GraftcodeInstrumentation, LOGGER_NAME

provider = TracerProvider()
instrumentation = GraftcodeInstrumentation.attach(provider, logging.getLogger(LOGGER_NAME))
...
instrumentation.close()
```

| Argument | Meaning |
|----------|---------|
| `tracer_provider` | Provider used to create the `Graftcode` tracer. Defaults to the global provider. |
| `logger` | A standard-library `logging.Logger`. Plays the role of `ILogger` in .NET or the OpenTelemetry logs bridge in Java. If it is omitted, Hypertube log lines are recorded as events on the current span. |

Names: `INSTRUMENTATION_NAME = "Graftcode"` (tracer name), `LOGGER_NAME = "Hypertube"` (logger name used by the App Insights connector).

## Settings (`GraftcodeTelemetrySettings`)

Static getters and setters, as in Java:

| Setting | Environment variable | Default |
|---------|----------------------|---------|
| `is_auto_inject_enabled` / `set_auto_inject_enabled` | `HYPERTUBE_TELEMETRY_AUTO_INJECT` | `true` |
| `is_body_capture_enabled` / `set_body_capture_enabled` | `HYPERTUBE_TELEMETRY_CAPTURE_BODIES` | `false` |
| `get_body_max_characters` / `set_body_max_characters` | `HYPERTUBE_TELEMETRY_BODY_MAX_CHARS` | `8192`, clamped to 256–32768 |

## Span model (`CallSpan`)

- **Caller** (`on_execute`): span `Graft <library> [version] <channel>`, kind `INTERNAL`. Attributes are `graft.dependency.type = Graft`, `graft.dependency.target`, `hypertube.runtime`, `hypertube.command_type`, `hypertube.connection_type`, `hypertube.operation`, and `hypertube.target` (with user info stripped).
- **Callee** (`on_process`): span named after the operation, for example `TestGgPythonLib.RandomService.RandomService.fetch`, kind `INTERNAL`.
- No span is opened for `PassByValue`, `RegisterForUpdate`, `ValueForUpdate`, `InvokeDelegate`, or any operation ending in `TelemetrySetup.Initialize`.
- When a call fails, the span gets `exception.type`, a redacted `exception.message`, and status `ERROR`.
- The span becomes current while the call runs (`opentelemetry.context.attach`) and is detached when the call completes. Code inside the called method therefore sees the callee span as its parent.

## Trace context

- **Outgoing:** the caller copies the thread-local connection headers without `tracestate`, injects W3C `traceparent`/`tracestate` plus `traceid`, and restores the previous headers when the call completes. A `traceparent` that the application set on the connection is never overwritten.
- **Incoming:** W3C headers from the `ContextMetadata` command are preferred. If only a `traceid` header arrives and it differs from the current trace, the callee joins that trace under a synthetic remote parent.
- **Called side:** on the called side, `ContextMetadata` is handled through `graftcode.context.request_context.RequestContext`, so the hosted library needs `graftcode-context` installed.

## Operation names (`OperationName`)

| Command | Name |
|---------|------|
| Member access (static/instance method, field, delegate) | `Type.member` |
| `CreateClassInstance` | `Type.ctor` |
| `InvokeGlobalFunction` / `GetGlobalField` | the function or field name |
| Anything else | `hypertube.<CommandType>` |

## Library catalog (`LibraryCatalog`)

`record` remembers the libraries loaded on a connection (`LoadLibrary`). `describe_target` formats `<library> [version] <channel>`, where the channel is the lowercased connection type. The library name is chosen in this order:

1. The `LoadLibrary` file stem.
2. The longest recorded library that prefixes the target type.
3. The root namespace of the target type.

Versions come from the jar manifest (`Implementation-Version`) or from `importlib.metadata` for Python packages. Without a library the target is `<runtime> <channel>`.

## Body capture (`BodyCapture`)

Body capture is off by default. When it is enabled, request and response commands are rendered as JSON-like text (`{"runtime":..,"type":..,"payload":[..]}`) on `hypertube.request.body` / `hypertube.response.body`:

- Sensitive keys, connection strings, bearer tokens and JWTs are redacted.
- Output is cut at the character budget; a cut body gets the `"__truncated__"` marker and the `.truncated` attribute.

## Logs

Hypertube writes its own console lines asynchronously (`AsyncLogSink`, writer thread `hypertube-log-writer`). Independently of that, the instrumentation receives every rendered line together with its template properties:

- **With a logger:** the line is forwarded at the matching level (`Trace`→5, `Debug`→10, `Info`→20, `Warning`→30, `Error`→40). Template properties are passed as `extra`, and the exception as `exc_info`. Names that clash with `LogRecord` attributes get a trailing `_`.
- **Without a logger:** the line becomes a span event carrying `log.level`. Exceptions become an `exception` event with redacted `exception.message` / `exception.stacktrace`.

## Tests

```powershell
$env:PYTHONPATH = "<HYPERTUBE>\src\python"
python -m pip install -e .[dev]
python -m pytest
```
