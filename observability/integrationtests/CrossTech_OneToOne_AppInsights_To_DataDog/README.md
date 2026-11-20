# CrossTech_OneToOne_AppInsights_To_DataDog

Cross-provider integration harness: **.NET** and **Node** use the **Application Insights** connectors; **Ruby** uses the **Datadog** connector (OTLP). W3C trace context links both sides of each call.

## Pairs (4)

| Caller | Provider (caller) | Callee | Provider (callee host) |
|--------|-------------------|--------|-------------------------|
| DotNet | App Insights | Ruby | Datadog |
| Ruby | Datadog | DotNet | App Insights |
| Node | App Insights | Ruby | Datadog |
| Ruby | Datadog | Node | App Insights |

## Prerequisites

| Item | Notes |
|------|--------|
| `APPLICATIONINSIGHTS_CONNECTION_STRING` | In `.env` (copy from App Insights suite) |
| Datadog Agent OTLP | `OTEL_EXPORTER_OTLP_ENDPOINT` (default `http://127.0.0.1:4318`) |
| Hosted libs | `TestGgLib`, `TestGgNodeLib`, `TestGgRubyLib` + `gg.exe` |
| Node / Ruby caller deps | Same as other matrix suites (`graft-packages`, gem feed `8808`) |

Dedicated ports: **9580** (DotNet), **9680** (Node), **9780** (Ruby).

## Run

```bat
Run-CrossTech_OneToOne_AppInsights_To_DataDog.cmd
```

```powershell
.\Run-CrossTech_OneToOne_AppInsights_To_DataDog.ps1
```

Options: `-SkipHosts`, `-IngestionWaitSeconds 90`

## Layout

| Path | Role |
|------|------|
| `matrix-config.json` | Hosts + explicit `pairs` |
| `dotnet/` | App Insights caller (DotNet → Ruby only) |
| `node/` | App Insights caller (Node → Ruby only) |
| `ruby/` | Datadog caller (Ruby → DotNet, Ruby → Node) |

## Results

`CrossTech_OneToOne_AppInsights_To_DataDog-results.csv` includes `CallerProvider`, `CalleeProvider`, and search hints for the **caller** trace id (App Insights `operation_Id` or Datadog `trace_id`).