using GraftCode.Netcore.Telemetry.AppInsightsConnector;

namespace Microsoft.Extensions.DependencyInjection
{
    public static class GraftCodeAppInsightsConnectorServiceCollectionExtensions
    {
        public static IServiceCollection AddGraftCodeAppInsightsConnector(
            this IServiceCollection services,
            string serviceName)
        {
            return services.AddGraftCodeAppInsightsConnector(
                options => options.ServiceName = serviceName);
        }

        public static IServiceCollection AddGraftCodeAppInsightsConnector(
            this IServiceCollection services,
            Action<GraftCodeAppInsightsConnectorOptions> configure)
        {
            if (services == null)
            {
                throw new ArgumentNullException(nameof(services));
            }

            if (configure == null)
            {
                throw new ArgumentNullException(nameof(configure));
            }

            var options = new GraftCodeAppInsightsConnectorOptions();
            configure(options);
            options.Validate();

            services.AddSingleton(options);
            services.AddSingleton<GraftCodeAppInsightsConnectorPipeline>(
                _ => GraftCodeAppInsightsConnector.Start(options));

            return services;
        }
    }
}
