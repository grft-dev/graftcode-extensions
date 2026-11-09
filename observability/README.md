# Observability packages

This directory contains OpenTelemetry instrumentation and Application Insights
connectors for .NET, Java, Python, and Node.js.

## Published packages

| Runtime | Instrumentation | Application Insights connector |
|---|---|---|
| .NET | `Graftcode.Instrumentation` | `GraftCode.Netcore.Telemetry.AppInsightsConnector` |
| Java | `com.graftcode:graftcode-instrumentation` | `com.graftcode:graftcode-appinsights-connector` |
| Python | `graftcode-instrumentation` | `graftcode-telemetry-appinsights` |
| Node.js | `graftcode-instrumentation` | `graftcode-appinsights-connector` |

The release workflow publishes all eight packages at the same version. Push a
tag named `observability-vX.Y.Z`, or run **Observability Packages** manually and
provide the package version. Pull requests and pushes to `main` only build and
test; they never publish.

## GitHub setup

Create a GitHub Actions Environment named `packages` and configure:

- `NPM_TOKEN`: an npm automation token with publish access to both public packages.
- `PIP_PUBLISH_TOKEN`: a PyPI API token with upload access to both Python projects.
- `MAVEN_CENTRAL_USERNAME` and `MAVEN_CENTRAL_PASSWORD`: a Sonatype Central
  Portal user token.
- `MAVEN_GPG_PRIVATE_KEY` and `MAVEN_GPG_PASSPHRASE`: the signing key and its
  passphrase. The public key must be published to a key server.
- NuGet Trusted Publishing is used for the .NET packages; configure the
  `packages` environment as a trusted publisher for the NuGet account.

The Maven Central publisher must have deployment rights for the existing
`com.graftcode` namespace. Package names must be created/reserved on PyPI,
npmjs.com, and NuGet.org before the first release.

## Upstream SDK requirement

The .NET and Java instrumentation projects use Hypertube APIs that are newer
than the currently published `3.1.18` SDK packages. The release workflow
deliberately blocks publishing until all package tests pass. Publish compatible
Hypertube SDK versions to NuGet and Maven Central, then update the corresponding
dependency versions here before creating an observability release tag.