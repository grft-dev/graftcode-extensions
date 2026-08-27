namespace Graft.Netcore.Telemetry.AppInsightsConnector
{
    public static class GraftAppInsightsConnector
    {
        public static GraftAppInsightsConnectorPipeline Start(string serviceName)
        {
            return Start(options => options.ServiceName = serviceName);
        }

        public static GraftAppInsightsConnectorPipeline Start(
            Action<GraftAppInsightsConnectorOptions> configure)
        {
            if (configure == null)
            {
                throw new ArgumentNullException(nameof(configure));
            }

            var options = new GraftAppInsightsConnectorOptions();
            configure(options);
            return Start(options);
        }

        public static GraftAppInsightsConnectorPipeline Start(
            GraftAppInsightsConnectorOptions options)
        {
            if (options == null)
            {
                throw new ArgumentNullException(nameof(options));
            }

            return new GraftAppInsightsConnectorPipeline(options);
        }
    }
}
