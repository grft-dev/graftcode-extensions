#if !NETSTANDARD2_1
using GraftCode.Netcore.Telemetry.AppInsightsConnector;
using Microsoft.AspNetCore.Builder;

namespace Microsoft.AspNetCore.Builder
{
    public static class GraftCodeAppInsightsConnectorApplicationBuilderExtensions
    {
        public static IApplicationBuilder UseGraftCodeAppInsightsConnectorRequestTelemetry(
            this IApplicationBuilder app)
        {
            return app.UseMiddleware<GraftCodeAppInsightsConnectorRequestTelemetryMiddleware>();
        }
    }
}
#endif
