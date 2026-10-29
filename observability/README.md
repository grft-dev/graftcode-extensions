# Observability packages

This directory contains OpenTelemetry instrumentation and Application Insights
connectors for .NET and Java. Node.js packages are not included in this release
workflow yet.

## Published packages

| Runtime | Instrumentation | Application Insights connector |
|---|---|---|
| .NET | `Graftcode.Instrumentation` | `GraftCode.Netcore.Telemetry.AppInsightsConnector` |
| Java | `com.graftcode:graftcode-instrumentation` | `com.graftcode:graftcode-appinsights-connector` |

The release workflow publishes all four packages at the same version. Push a tag
named `observability-vX.Y.Z`, or run **Observability Packages** manually and
provide the package version. Pull requests and pushes to `main` only build and
test; they never publish.

## GitHub setup

Create a GitHub Actions Environment named `packages` and add these secrets:

- `NUGET_API_KEY`: an API key with push access on nuget.org.
- `MAVEN_CENTRAL_USERNAME` and `MAVEN_CENTRAL_PASSWORD`: a Sonatype Central
  Portal user token.
- `MAVEN_GPG_PRIVATE_KEY` and `MAVEN_GPG_PASSPHRASE`: the signing key and its
  passphrase. The public key must be published to a key server.

The Maven Central publisher must have deployment rights for the existing
`com.graftcode` namespace. NuGet package IDs are reserved when their first
package is pushed.

## Upstream SDK requirement

The .NET and Java instrumentation projects use Hypertube APIs that are newer
than the currently published `3.1.18` SDK packages. The release workflow
deliberately blocks publishing until all package tests pass. Publish compatible
Hypertube SDK versions to NuGet and Maven Central, then update the corresponding
dependency versions here before creating an observability release tag.