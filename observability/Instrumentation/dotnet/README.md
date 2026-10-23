# Graftcode.Instrumentation

OpenTelemetry spans and `ILogger` forwarding for Graftcode invocations.

The package references `Hypertube.Netcore.Utils` and attaches itself to Hypertube's
`HypertubeBroker` and `HypertubeLogger` through the abstractions in
`Hypertube.Netcore.Utils.Abstractions`. Commands are consumed through Hypertube's
`ICommand` abstraction. Hypertube has no reference to this package; without it, Hypertube
records no spans and forwards no logs.

## Usage

```csharp
using var tracing = Sdk.CreateTracerProviderBuilder()
    .AddGraftcodeInstrumentation(loggerFactory.CreateLogger("Hypertube"))
    .AddYourExporter()
    .Build();
```

- Spans are emitted from the `Graftcode` activity source.
- When a logger is passed, Hypertube log lines are forwarded to it; otherwise they are
  recorded as events on the current span.
- Instrumentation is active for the lifetime of the tracer provider. If several providers
  register it, the last one wins.

## Hypertube dependency

By default this package references **`Hypertube.Netcore.Utils`** from NuGet (`Version="*"`),
so CI and other machines do not need a local Hypertube checkout.

For local Hypertube development, build with a project reference instead:

```powershell
dotnet build -p:UseHypertubeUtilsProject=true -p:HypertubeAssemblyVersion=3.1.18.0
```

`HypertubeUtilsProject` overrides the path to the Utils `.csproj`. If `../../../../NewHT/HYPERTUBE/...` exists relative to this folder, `Directory.Build.props`
sets `UseHypertubeUtilsProject` automatically.

At runtime, Hypertube loads Utils by exact assembly version, so the Utils version on the host
must match the Utils this library was compiled against. The current **Hypertube.Netcore.Utils**
on NuGet must be republished with the abstractions and `ICommand` APIs before remote-only builds
(that rely on `Version="*"`) can compile against it.

## Request and response bodies

Body capture is off by default (`HYPERTUBE_TELEMETRY_CAPTURE_BODIES`,
`HYPERTUBE_TELEMETRY_BODY_MAX_CHARS`). Enabling it may expose PII or secrets in telemetry; it is
intended only for controlled dev/staging environments or explicitly approved production
diagnostics. Known sensitive keys, headers, tokens and connection strings are redacted with
Hypertube's `LogsRedactor` before anything is recorded.
