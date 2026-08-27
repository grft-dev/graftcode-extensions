#if !NETSTANDARD2_1
using Graft.Netcore.Telemetry.AppInsightsConnector;
using Microsoft.AspNetCore.Builder;

namespace Microsoft.AspNetCore.Builder
{
    public static class GraftAppInsightsConnectorApplicationBuilderExtensions
    {
        public static IApplicationBuilder UseGraftAppInsightsConnectorRequestTelemetry(
            this IApplicationBuilder app)
        {
            return app.UseMiddleware<GraftAppInsightsConnectorRequestTelemetryMiddleware>();
        }
    }
}
#endif
