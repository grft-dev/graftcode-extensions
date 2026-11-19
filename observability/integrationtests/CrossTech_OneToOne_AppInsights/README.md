# CrossTech_OneToOne_AppInsights

Cross-technology **4×4** integration harness: each **caller** (.NET, Java, Python, Node) invokes each **callee** `RandomService.fetch` on local **gg** hosts and exports telemetry through the **Graftcode Application Insights connectors**.

Output: console table + `CrossTech_OneToOne_AppInsights-results.csv` with `TraceId` (`operation_Id` in Application Insights).

## Prerequisites

| Item | Location / notes |
|------|------------------|
| Application Insights | `APPLICATIONINSIGHTS_CONNECTION_STRING` in `.env` (copy from `.env.example`) |
| `gg.exe` + hosted libs built | `C:\dev\TestGgLib`, `TestGgJavaLib`, `TestGgNodeLib`, `TestGgPythonLib` |
| .NET connector (project ref) | Built with the runner or via `dotnet build` on this repo’s connector |
| Java connector in local Maven | `mvn install` on `graftcode-extensions/observability/opentelemetry/java/appinsights` |
| Python connector in venv | `C:\dev\TestGgPythonWebApi\.venv` (graftcode-telemetry-appinsights + hypertube) |
| Node packages | `C:\dev\graft-packages\*.tgz` (script runs `npm ci` in `node/` on first use) |
| Maven (optional Java caller) | `C:\dev\_tools\apache-maven-3.9.9\bin\mvn.cmd` or `MAVEN_HOME` |

Build hosted samples once:

```powershell
dotnet build C:\dev\TestGgLib\TestGgLib\TestGgLib.csproj -c Release
mvn -q -f C:\dev\TestGgJavaLib\pom.xml -DskipTests package
```

## Run (single command)

From this folder:

```bat
Run-CrossTech_OneToOne_AppInsights.cmd
```

Or PowerShell:

```powershell
.\Run-CrossTech_OneToOne_AppInsights.ps1
```

This will:

1. Load `.env`
2. Prepare ports and start **four gg hosts** (`matrix-config.json` → 8080 / 9080 / 9180 / 9280)
3. Build and run all four **callers** (16 Hypertube WS calls)
4. Wait **90s** for ingestion (override: `-IngestionWaitSeconds 30`)
5. Print the results table and write the CSV

If hosts are already running:

```powershell
.\Run-CrossTech_OneToOne_AppInsights.ps1 -SkipHosts
```

## Layout

| Path | Role |
|------|------|
| `Run-CrossTech_OneToOne_AppInsights.ps1` | Orchestrator |
| `matrix-config.json` | Host ports, WS URLs, gg start commands, type/method names |
| `dotnet/` | .NET caller (`CrossTechOneToOneAppInsightsRunner`) |
| `java/` | Java caller (`CrossTechOneToOneRunner`) |
| `python/matrix_caller.py` | Python caller |
| `node/matrix_caller.mjs` | Node caller |
| `logs/` | gg host stdout/stderr when started by the script |

## Telemetry shape

Each pair uses:

1. **`CrossMatrix.{Caller}->{Callee}`** span (`SERVER` / `ActivityKind.Server`)
2. **`CrossMatrix Caller=…`** log under that span
3. Graft **InProc** dependencies from graftcode instrumentation
4. **`forceFlush` / `flush` after the span ends**

Node connector excludes Application Insights **ingest** HTTP and WebSocket upgrades from HTTP auto-instrumentation.

## Application Insights

Use `TraceId` from the CSV as `operation_Id`, or the `AppInsightsSearch` column query.

Legacy Docker / Web API matrix assets remain under `C:\dev\TestGgCrossMatrix` (not part of this suite).