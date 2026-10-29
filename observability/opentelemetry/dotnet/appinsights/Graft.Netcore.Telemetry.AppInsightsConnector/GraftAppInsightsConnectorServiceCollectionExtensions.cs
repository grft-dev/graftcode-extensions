using Graft.Netcore.Telemetry.AppInsightsConnector;

namespace Microsoft.Extensions.DependencyInjection
{
    public static class GraftAppInsightsConnectorServiceCollectionExtensions
    {
        public static IServiceCollection AddGraftAppInsightsConnector(
            this IServiceCollection services,
            string serviceName)
        {
            return services.AddGraftAppInsightsConnector(
                options => options.ServiceName = serviceName);
        }

        public static IServiceCollection AddGraftAppInsightsConnector(
            this IServiceCollection services,
            Action<GraftAppInsightsConnectorOptions> configure)
        {
            if (services == null)
            {
                throw new ArgumentNullException(nameof(services));
            }

            if (configure == null)
            {
                throw new ArgumentNullException(nameof(configure));
            }

            var options = new GraftAppInsightsConnectorOptions();
            configure(options);
            options.Validate();

            services.AddSingleton(options);
            services.AddSingleton<GraftAppInsightsConnectorPipeline>(
                _ => GraftAppInsightsConnector.Start(options));

            return services;
        }
    }
}
