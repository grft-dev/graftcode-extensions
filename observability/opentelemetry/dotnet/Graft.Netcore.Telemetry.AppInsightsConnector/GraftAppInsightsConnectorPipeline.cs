using System.Diagnostics;
using Azure.Monitor.OpenTelemetry.Exporter;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using OpenTelemetry;
using OpenTelemetry.Logs;
using OpenTelemetry.Resources;
using OpenTelemetry.Trace;

namespace Graft.Netcore.Telemetry.AppInsightsConnector
{
    public sealed class GraftAppInsightsConnectorPipeline : IDisposable
    {
        private readonly object _flushGate = new object();
        private readonly ServiceProvider _loggingServices;
        private readonly LoggerProvider? _loggerProvider;
        private readonly TracerProvider _tracerProvider;
        private bool _disposed;

        internal GraftAppInsightsConnectorPipeline(GraftAppInsightsConnectorOptions options)
        {
            options.Validate();

            Activity.DefaultIdFormat = ActivityIdFormat.W3C;
            Activity.ForceDefaultIdFormat = true;

            ServiceName = options.ServiceName;
            ActivitySource = new ActivitySource(ServiceName);

            var resource = ResourceBuilder.CreateDefault().AddService(ServiceName);
            var connectionString = options.ResolveConnectionString();
            IsExportingToAppInsightsConnector = connectionString != null;

            var tracing = Sdk.CreateTracerProviderBuilder()
                .SetResourceBuilder(resource)
                .AddSource(options.GetActivitySourceNames());

            if (connectionString != null)
            {
                tracing.AddAzureMonitorTraceExporter(exporter =>
                {
                    exporter.ConnectionString = connectionString;
                    exporter.TracesPerSecond = options.TracesPerSecond;
                });

                if (options.TracesPerSecond == null)
                {
                    tracing.SetSampler(new AlwaysOnSampler());
                }
            }

            _tracerProvider = tracing.Build();

            var loggingServices = new ServiceCollection();
            loggingServices.AddLogging(logging =>
            {
                logging.SetMinimumLevel(options.MinimumLogLevel);

                if (options.EnableConsoleLogging)
                {
                    logging.AddConsole();
                }

                if (connectionString != null)
                {
                    logging.AddOpenTelemetry(openTelemetry =>
                    {
                        openTelemetry.IncludeFormattedMessage = true;
                        openTelemetry.IncludeScopes = true;
                        openTelemetry.SetResourceBuilder(resource);
                        openTelemetry.AddAzureMonitorLogExporter(
                            exporter => exporter.ConnectionString = connectionString);
                    });
                }
            });

            _loggingServices = loggingServices.BuildServiceProvider();

            _loggerProvider = _loggingServices.GetService<LoggerProvider>();

            Logger = _loggingServices
                .GetRequiredService<ILoggerFactory>()
                .CreateLogger(ServiceName);

            if (!IsExportingToAppInsightsConnector)
            {
                WarnAboutMissingConnectionString(options);
            }

            AppDomain.CurrentDomain.ProcessExit += OnProcessExit;
            AppDomain.CurrentDomain.UnhandledException += OnUnhandledException;
            Console.CancelKeyPress += OnCancelKeyPress;
        }

        public string ServiceName { get; }

        public ActivitySource ActivitySource { get; }

        public ILogger Logger { get; }

        public bool IsExportingToAppInsightsConnector { get; }

        public Activity? StartActivity(
            string name,
            ActivityKind kind = ActivityKind.Internal)
        {
            return ActivitySource.StartActivity(name, kind);
        }

        public bool ForceFlush(int millisecondsTimeout = 5000)
        {
            lock (_flushGate)
            {
                if (_disposed)
                {
                    return true;
                }

                var tracesFlushed = _tracerProvider.ForceFlush(millisecondsTimeout);
                var logsFlushed = _loggerProvider == null
                    || _loggerProvider.ForceFlush(millisecondsTimeout);

                return tracesFlushed && logsFlushed;
            }
        }

        public void Dispose()
        {
            lock (_flushGate)
            {
                if (_disposed)
                {
                    return;
                }

                _tracerProvider.ForceFlush();
                _loggerProvider?.ForceFlush();
                _disposed = true;
            }

            AppDomain.CurrentDomain.ProcessExit -= OnProcessExit;
            AppDomain.CurrentDomain.UnhandledException -= OnUnhandledException;
            Console.CancelKeyPress -= OnCancelKeyPress;

            _loggingServices.Dispose();
            _tracerProvider.Dispose();
            ActivitySource.Dispose();
        }

        private void WarnAboutMissingConnectionString(GraftAppInsightsConnectorOptions options)
        {
            const string message =
                "Application Insights telemetry for {ServiceName} is disabled: no connection string was configured. "
                + "Set APPLICATIONINSIGHTS_CONNECTION_STRING in this process or assign "
                + "GraftAppInsightsConnectorOptions.ConnectionString.";

            Logger.LogWarning(message, ServiceName);

            if (!options.EnableConsoleLogging)
            {
                Console.Error.WriteLine(message.Replace("{ServiceName}", ServiceName));
            }
        }

        private void OnProcessExit(object? sender, EventArgs eventArgs)
        {
            Dispose();
        }

        private void OnUnhandledException(object? sender, UnhandledExceptionEventArgs eventArgs)
        {
            ForceFlush();
        }

        private void OnCancelKeyPress(object? sender, ConsoleCancelEventArgs eventArgs)
        {
            ForceFlush();
        }
    }
}
