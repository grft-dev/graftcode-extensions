using Microsoft.Extensions.Logging;

namespace Graft.Netcore.Telemetry.AppInsightsConnector
{
    public sealed class GraftAppInsightsConnectorOptions
    {
        private readonly HashSet<string> _activitySourceNames =
            new HashSet<string>(StringComparer.Ordinal);

        public string ServiceName { get; set; } = string.Empty;

        public string? ConnectionString { get; set; }

        public LogLevel MinimumLogLevel { get; set; } = LogLevel.Information;

        public double? TracesPerSecond { get; set; }

        public bool EnableConsoleLogging { get; set; } = true;

        public ISet<string> ActivitySourceNames => _activitySourceNames;

        internal string? ResolveConnectionString()
        {
            if (!string.IsNullOrWhiteSpace(ConnectionString))
            {
                return ConnectionString;
            }

            var environmentValue =
                Environment.GetEnvironmentVariable("APPLICATIONINSIGHTS_CONNECTION_STRING");

            return string.IsNullOrWhiteSpace(environmentValue) ? null : environmentValue;
        }

        internal string[] GetActivitySourceNames()
        {
            var names = new HashSet<string>(_activitySourceNames, StringComparer.Ordinal)
            {
                ServiceName,
                "Graftcode",
            };

            names.RemoveWhere(string.IsNullOrWhiteSpace);
            return names.ToArray();
        }

        internal void Validate()
        {
            if (string.IsNullOrWhiteSpace(ServiceName))
            {
                throw new InvalidOperationException(
                    $"{nameof(ServiceName)} must be configured for Azure Monitor telemetry.");
            }
        }
    }
}
