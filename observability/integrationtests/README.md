# Observability integration tests

| Suite | Description |
|-------|-------------|
| [CrossTech_OneToOne_AppInsights](./CrossTech_OneToOne_AppInsights/) | 4×4 caller/callee matrix over local gg hosts → Application Insights |
| [CrossTech_OneToOne_DataDog](./CrossTech_OneToOne_DataDog/) | 2×2 PHP/Ruby matrix over local gg hosts → Datadog (OTLP) |
| [CrossTech_OneToOne_AppInsights_To_DataDog](./CrossTech_OneToOne_AppInsights_To_DataDog/) | .NET/Node (App Insights) ↔ Ruby (Datadog), 4 cross-provider pairs |

Start with [CrossTech_OneToOne_AppInsights/README.md](./CrossTech_OneToOne_AppInsights/README.md).