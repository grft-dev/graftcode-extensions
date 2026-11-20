# CrossTech_OneToOne_DataDog

Cross-technology **2×2** integration harness: **PHP** and **Ruby** each call each other's local **gg** host (`RandomService.fetch`) and export telemetry through the **Graftcode Datadog connectors** (OTLP to the Datadog Agent).

Output: console table + `CrossTech_OneToOne_DataDog-results.csv` with `TraceId` for Datadog APM search.

## Pairs (4)

| Caller | Callee |
|--------|--------|
| Php | Php |
| Php | Ruby |
| Ruby | Php |
| Ruby | Ruby |

## Prerequisites

| Item | Location / notes |
|------|------------------|
| Datadog Agent | OTLP HTTP on `4318` (default `OTEL_EXPORTER_OTLP_ENDPOINT`) |
| `.env` | Copy from `.env.example` (optional `DD_ENV`, `DD_VERSION`) |
| `gg.exe` + sample libs | `C:\dev\TestGgPhpLib`, `C:\dev\TestGgRubyLib` |
| PHP caller deps | `composer install` in `C:\dev\TestGgPhpWebApi` (hypertube SDK + datadog connector) |
| Ruby gems feed | `http://127.0.0.1:8808` with `graftcode-telemetry-datadog` and `graftcode-instrumentation` |
| Ruby runtime | `C:\Ruby34-x64` (or set `RUBY_HOME`) |
| PHP CLI | `C:\php84\php.exe` or `php` on PATH |

Hosts listen on **9380** (Php) and **9480** (Ruby) so they do not clash with the App Insights matrix suite.

## Run (single command)

```bat
Run-CrossTech_OneToOne_DataDog.cmd
```

Or PowerShell:

```powershell
.\Run-CrossTech_OneToOne_DataDog.ps1
```

This will:

1. Load `.env`
2. Start **two gg hosts** (unless `-SkipHosts`)
3. Run **PHP** and **Ruby callers** (4 Hypertube WS calls)
4. Wait **30s** for OTLP export (override: `-IngestionWaitSeconds 90`)
5. Print the results table and write the CSV

## Layout

| Path | Role |
|------|------|
| `Run-CrossTech_OneToOne_DataDog.ps1` | Orchestrator |
| `matrix-config.json` | Host ports, WS URLs, type/method names |
| `php/matrix_caller.php` | PHP caller |
| `ruby/matrix_caller.rb` | Ruby caller |
| `ruby/Gemfile` | Ruby caller bundle (Datadog connector + hypertube SDK) |
| `logs/` | gg host stdout/stderr when started by the script |

## Telemetry shape

Each pair uses:

1. **`CrossMatrix.{Caller}->{Callee}`** span (`SERVER` / `SpanKind.SERVER` / `kind: :server`)
2. **`CrossMatrix Caller=…`** log under that span
3. Graft **InProc** dependencies from graftcode instrumentation
4. **`flush`** after the span ends

## Datadog

Use `TraceId` from the CSV in APM (`trace_id:` filter) or the `DatadogSearch` column from the results table.