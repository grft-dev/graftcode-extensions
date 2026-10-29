# Observability packages

This directory contains OpenTelemetry instrumentation and Application Insights
connectors for .NET, Java, and Node.js.

## Published packages

| Runtime | Instrumentation | Application Insights connector |
|---|---|---|
| .NET | `Graftcode.Instrumentation` | `GraftCode.Netcore.Telemetry.AppInsightsConnector` |
| Java | `com.graftcode:graftcode-instrumentation` | `com.graftcode:graftcode-appinsights-connector` |
| Node.js | `graftcode-instrumentation` | `graftcode-appinsights-connector` |

The release workflow publishes all six packages at the same version. Push a tag
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
- `NPM_TOKEN`: an npm automation token with publish access to both package
  names.

The Maven Central publisher must have deployment rights for the existing
`com.graftcode` namespace. The npm package names must be owned by the publishing
account before the first release. NuGet package IDs are reserved when their first
package is pushed.

## Upstream SDK requirement

The instrumentation projects use Hypertube APIs that are newer than the
currently published `3.1.18` SDK packages. The release workflow deliberately
blocks publishing until all package tests pass. Publish the compatible Hypertube
SDK versions to npm, Maven Central, and NuGet, then update the corresponding
dependency versions here before creating an observability release tag.