#if !NETSTANDARD2_1
using System.Diagnostics;
using Microsoft.AspNetCore.Http;

namespace Graft.Netcore.Telemetry.AppInsightsConnector
{
    public sealed class GraftAppInsightsConnectorRequestTelemetryMiddleware
    {
        private readonly RequestDelegate _next;
        private readonly GraftAppInsightsConnectorPipeline _telemetry;

        public GraftAppInsightsConnectorRequestTelemetryMiddleware(
            RequestDelegate next,
            GraftAppInsightsConnectorPipeline telemetry)
        {
            _next = next;
            _telemetry = telemetry;
        }

        public async Task InvokeAsync(HttpContext context)
        {
            var operationName = ResolveOperationName(context, _telemetry.ServiceName);
            using var activity = _telemetry.StartActivity(operationName, ActivityKind.Server);

            if (activity != null)
            {
                activity.SetTag("http.method", context.Request.Method);
                activity.SetTag("http.route", context.Request.Path.Value);
            }

            try
            {
                await _next(context);

                if (activity != null)
                {
                    activity.SetTag("http.status_code", context.Response.StatusCode);

                    if (context.Response.StatusCode >= 500)
                    {
                        activity.SetStatus(ActivityStatusCode.Error);
                    }
                }
            }
            catch (Exception exception)
            {
                if (activity != null)
                {
                    activity.SetStatus(ActivityStatusCode.Error, exception.Message);
                    activity.AddException(exception);
                }

                throw;
            }
        }

        internal static string ResolveOperationName(HttpContext context, string serviceName)
        {
            if (context.Request.RouteValues.TryGetValue("controller", out var controller)
                && controller is string controllerName
                && !string.IsNullOrWhiteSpace(controllerName))
            {
                return $"{serviceName}.{controllerName}";
            }

            var path = context.Request.Path.HasValue
                ? context.Request.Path.Value
                : "/";

            return $"{serviceName} {context.Request.Method} {path}";
        }
    }
}
#endif
