# Graft App Insights Connector

Configure Application Insights for an application and Graft bridge spans with one call:

```csharp
using Graft.Netcore.Telemetry.AppInsightsConnector;

using var telemetry = GraftAppInsightsConnector.Start("MyService");
using var activity = telemetry.StartActivity("MyOperation");

telemetry.Logger.LogInformation("My operation started");
```

The connector reads `APPLICATIONINSIGHTS_CONNECTION_STRING`, exports logs and traces,
and automatically subscribes to the application's source and the `Graftcode`
source. An explicit connection string and additional activity sources can be configured:

```csharp
using var telemetry = GraftAppInsightsConnector.Start(options =>
{
    options.ServiceName = "MyService";
    options.ConnectionString = configuration["ApplicationInsights:ConnectionString"];
    options.ActivitySourceNames.Add("MyCompany.Shared");
});
```

For dependency-injection applications:

```csharp
services.AddGraftAppInsightsConnector(options =>
{
    options.ServiceName = "MyService";
});
```

Resolve `GraftAppInsightsConnectorPipeline` to access its `ActivitySource`, `Logger`, and
`ForceFlush()` method.

For ASP.NET Core applications, register request telemetry after routing:

```csharp
app.UseRouting();
app.UseGraftAppInsightsConnectorRequestTelemetry();
app.MapControllers();
```

Each HTTP request gets a server `Activity` named `{ServiceName}.{controller}` when route
values are available, for example `TestGgWebApi.Fetch`. Logs and downstream bridge spans
inside the request then share the same trace automatically.

## Sampling

Every trace is kept by default. Azure Monitor's own default is a rate limit of five traces
per second, which drops whole traces once the budget is spent - and because unsampled
traces also lose their log records, a short-lived caller that makes a single bridge call
can end up reporting nothing at all while the long-running side it called reports normally.

Set `TracesPerSecond` to opt into rate limiting for high-volume services:

```csharp
options.TracesPerSecond = 5;
```

## Flushing

`ForceFlush()` covers both traces and logs, so a short-lived process should call it before
exiting. The pipeline also flushes on `Dispose()`, process exit, Ctrl+C, and unhandled
exceptions; telemetry is still lost when a process is killed outright, for example by
stopping a debugging session.

## Diagnosing missing telemetry

`IsExportingToAppInsightsConnector` is false when no connection string was resolved, in which case
the connector logs a warning and telemetry stays local. This is worth asserting on startup
for services that must report, since a connection string set at machine level only reaches
processes started afterwards, and does not reach containers at all.
