namespace GraftCode.Netcore.Telemetry.AppInsightsConnector
{
    public static class GraftCodeAppInsightsConnector
    {
        public static GraftCodeAppInsightsConnectorPipeline Start(string serviceName)
        {
            return Start(options => options.ServiceName = serviceName);
        }

        public static GraftCodeAppInsightsConnectorPipeline Start(
            Action<GraftCodeAppInsightsConnectorOptions> configure)
        {
            if (configure == null)
            {
                throw new ArgumentNullException(nameof(configure));
            }

            var options = new GraftCodeAppInsightsConnectorOptions();
            configure(options);
            return Start(options);
        }

        public static GraftCodeAppInsightsConnectorPipeline Start(
            GraftCodeAppInsightsConnectorOptions options)
        {
            if (options == null)
            {
                throw new ArgumentNullException(nameof(options));
            }

            return new GraftCodeAppInsightsConnectorPipeline(options);
        }
    }
}
