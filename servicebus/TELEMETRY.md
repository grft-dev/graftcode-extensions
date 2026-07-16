# Service Bus plugin telemetry

This document describes how Service Bus telemetry works in the plugin today and which
Hypertube / graftcode-gateway integration points are needed for end-to-end OpenTelemetry
and Application Insights visibility.

## Goals

| Layer | What should appear in telemetry |
|-------|----------------------------------|
| Application | Existing graft spans (`Graft → ...`, business methods) via `traceparent` in hypertube payload |
| Transport | Azure Service Bus RPC as a dependency (`queue-01` → `queue-02`, duration, success) |
| Broker hop | W3C trace context on AMQP application properties for cross-process correlation |

Application-level correlation already works through hypertube `ContextMetadata` embedded in
the graft payload. This plugin change adds **broker-level propagation** and **transport
span metadata** that Hypertube can turn into dependency telemetry.

## Plugin implementation (this branch)

### AMQP application properties

On **send**, when invocation context is set:

| Property | Value |
|----------|--------|
| `traceparent` | W3C trace context |
| `tracestate` | Optional W3C tracestate |
| `Diagnostic-Id` | Same as `traceparent` for Azure SDK / legacy AI consumers |

On **receive**, the server plugin extracts these properties, sets thread-local invocation
context for the duration of `processMessage`, and records a transport span.

### Exported C API (implemented now)

These functions are exported from `libServiceBusPlugin.so` and can be resolved by Hypertube
before the generic `CreateTransportChannel` factory:

```c
void SetServiceBusInvocationContext(const char* traceparent, const char* tracestate);
void ClearServiceBusInvocationContext();
const char* GetServiceBusLastTransportTelemetryJson();
const char* GetServiceBusCurrentInvocationContextJson();
```

`GetServiceBusLastTransportTelemetryJson()` returns JSON like:

```json
{
  "transport": "azure.servicebus",
  "operation": "rpc",
  "queue": "queue-01",
  "replyQueue": "queue-02",
  "correlationId": "graftcode-...",
  "durationMs": 42,
  "success": true,
  "traceparent": "00-abc...-def...-01"
}
```

## Proposed Hypertube integration

### 1. Client: `ITransport` / `NativeTransmitter` (highest priority)

**Where:** `HYPERTUBE/src/native/HypertubeNative/Transmitter/NativeTransmitter.cpp`

**When:** Immediately before `transportChannel->SendCommand(...)` for `ChannelType::PLUGIN`.

**What to do:**

1. Read thread-local graft headers already used for WebSocket (`traceparent`, `tracestate`).
2. `dlsym` optional plugin exports:
   - `SetServiceBusInvocationContext`
   - `ClearServiceBusInvocationContext`
3. Call `SetServiceBusInvocationContext(traceparent, tracestate)`.
4. Execute `SendCommand` / `ReadResponse`.
5. Call `GetServiceBusLastTransportTelemetryJson()`.
6. Forward JSON to the managed runtime (new hypertube export or existing App Insights hook).
7. Call `ClearServiceBusInvocationContext()` in `finally`.

**Proposed interface:** see `GraftcodePluginsInterfaces/ITransportTelemetry.h`

```cpp
class ITransportTelemetry {
  virtual void SetInvocationContext(const TransportInvocationContext& context) = 0;
  virtual const char* GetLastTransportTelemetryJson() = 0;
  virtual void ClearInvocationContext() = 0;
};
```

Hypertube can probe for this interface or fall back to the exported C API above.

### 2. Managed .NET SDK: dependency emission

**Where:**

- `HYPERTUBE/src/netcore/Hypertube.Netcore.Core/Interpreters/Interpreter.cs`
- or `GraftCodeNetcoreLogger` integration layer in consuming apps

**When:** After native `Transmitter.SendCommand` returns for plugin transports.

**What to emit:**

```text
DependencyTelemetry
  Type = "Azure Service Bus" (or "graft.plugin.servicebus")
  Name = "RPC queue-01"
  Target = "queue-01"
  Data = reply queue + correlationId
  Duration = durationMs from plugin JSON
  Success = success
  Context.Operation.Id = traceparent trace id
  Context.Operation.ParentId = current graft span id
```

This is what makes Service Bus visible in Application Insights end-to-end views alongside
`Graft → TemperatureFacade.ConvertCelsiusToFahrenheit`.

### 3. Server: `IServer` / graftcode-gateway `gg`

**Where:**

- `graftcode-gateway` message handler before invoking the hosted module
- `GraftcodePluginsInterfaces/IServerTelemetry.h`

**When:** After the Service Bus consumer receives a message and before user code runs.

**What to do:**

1. Call `GetServiceBusCurrentInvocationContextJson()` on the server thread.
2. Map `traceparent` into `Graftcode.Context.RequestContext` headers (same as HTTP/h2 today).
3. Optionally emit a server-side `DependencyTelemetry` / Activity for `operation=process`.

**Proposed callback shape:**

```cpp
using ProcessMessageWithContextFn = bool(*)(
    const byte* request,
    std::size_t requestSize,
    const ServerInvocationContext* context,
    WriteResponseFn writeResponse,
    void* writeContext);
```

Backward compatible approach: keep existing `ProcessMessageFn` and add an optional
`configureTelemetry(...)` hook on `IServer`.

### 4. Optional: plugin config

No change required for basic telemetry. Optional future config keys:

```json
{
  "name": "ServiceBusPlugin",
  "queue": "queue-01",
  "replyQueue": "queue-02",
  "telemetry": {
    "propagateTraceContext": true,
    "emitTransportSpans": true
  }
}
```

## What is intentionally out of scope here

- Azure Monitor auto-instrumentation for `Azure.Messaging.ServiceBus` — the plugin uses
  `azure-core-amqp-cpp` directly, not the .NET Service Bus SDK.
- Service Bus namespace metrics — configure via Azure Portal diagnostic settings.
- Replacing application graft spans — those remain the responsibility of
  `GraftCodeNetcoreLogger` / `GraftTelemetry`.

## Demo validation

After Hypertube wires the client hook, Application Insights should show for one weather request:

1. `GetWeather` (CityWeather)
2. `Graft → TemperatureFacade.ConvertCelsiusToFahrenheit` (CityWeather)
3. **Dependency** `Azure Service Bus` / `queue-01` (new)
4. `ConvertCelsiusToFahrenheit` (Temperature service, correlated via `traceparent`)

## Related files

- `ServiceBusPlugin/ServiceBusTelemetry.h`
- `ServiceBusPlugin/ServiceBusTelemetry.cpp`
- `GraftcodePluginsInterfaces/ITransportTelemetry.h`
- `GraftcodePluginsInterfaces/IServerTelemetry.h`
