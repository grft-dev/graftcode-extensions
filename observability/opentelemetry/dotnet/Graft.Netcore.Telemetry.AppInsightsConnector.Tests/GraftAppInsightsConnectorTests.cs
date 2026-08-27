using System.Diagnostics;
using Microsoft.AspNetCore.Http;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using Xunit;

namespace Graft.Netcore.Telemetry.AppInsightsConnector.Tests
{
    [CollectionDefinition(EnvironmentCollection.Name, DisableParallelization = true)]
    public sealed class EnvironmentCollection
    {
        public const string Name = "AppInsightsConnectorEnvironment";
    }

    [Collection(EnvironmentCollection.Name)]
    public sealed class GraftAppInsightsConnectorTests
    {
        private const string LocalConnectionString =
            "InstrumentationKey=00000000-0000-0000-0000-000000000001;"
            + "IngestionEndpoint=http://127.0.0.1:1/;LiveEndpoint=http://127.0.0.1:1/";

        [Fact]
        public void Start_RequiresAServiceName()
        {
            Assert.Throws<InvalidOperationException>(
                () => GraftAppInsightsConnector.Start(new GraftAppInsightsConnectorOptions()));
        }

        [Fact]
        public void ExplicitConnectionString_WinsOverEnvironment()
        {
            const string environmentValue = "InstrumentationKey=11111111-1111-1111-1111-111111111111";
            const string explicitValue = "InstrumentationKey=22222222-2222-2222-2222-222222222222";
            var previous = Environment.GetEnvironmentVariable("APPLICATIONINSIGHTS_CONNECTION_STRING");

            try
            {
                Environment.SetEnvironmentVariable(
                    "APPLICATIONINSIGHTS_CONNECTION_STRING",
                    environmentValue);

                var options = new GraftAppInsightsConnectorOptions
                {
                    ServiceName = "test",
                    ConnectionString = explicitValue,
                };

                Assert.Equal(explicitValue, options.ResolveConnectionString());
            }
            finally
            {
                Environment.SetEnvironmentVariable(
                    "APPLICATIONINSIGHTS_CONNECTION_STRING",
                    previous);
            }
        }

        [Fact]
        public void Sources_AlwaysContainTheServiceAndGraftcode()
        {
            var options = new GraftAppInsightsConnectorOptions { ServiceName = "test.service" };
            options.ActivitySourceNames.Add("custom.source");

            var sources = options.GetActivitySourceNames();

            Assert.Contains("test.service", sources);
            Assert.Contains("Graftcode", sources);
            Assert.Contains("custom.source", sources);
        }

        [Fact]
        public void Start_WorksWithoutAConnectionString()
        {
            var previous = Environment.GetEnvironmentVariable("APPLICATIONINSIGHTS_CONNECTION_STRING");

            try
            {
                Environment.SetEnvironmentVariable("APPLICATIONINSIGHTS_CONNECTION_STRING", null);

                using var pipeline = GraftAppInsightsConnector.Start(options =>
                {
                    options.ServiceName = "test.service";
                    options.EnableConsoleLogging = false;
                });
                using var activity = pipeline.StartActivity("test.operation");

                Assert.NotNull(activity);
                Assert.Equal(ActivityIdFormat.W3C, activity!.IdFormat);
                Assert.True(pipeline.ForceFlush());
            }
            finally
            {
                Environment.SetEnvironmentVariable(
                    "APPLICATIONINSIGHTS_CONNECTION_STRING",
                    previous);
            }
        }

        [Fact]
        public void EveryTraceIsRecorded_SoLogsAreNotSampledAway()
        {
            using var pipeline = GraftAppInsightsConnector.Start(options =>
            {
                options.ServiceName = "test.service";
                options.EnableConsoleLogging = false;
                options.ConnectionString = LocalConnectionString;
            });

            for (var i = 0; i < 25; i++)
            {
                using var activity = pipeline.StartActivity($"test.operation.{i}", ActivityKind.Server);

                Assert.NotNull(activity);
                Assert.True(activity!.Recorded, $"activity {i} was not recorded");
            }
        }

        [Fact]
        public void RateLimitedSampling_CanBeOptedInto()
        {
            using var pipeline = GraftAppInsightsConnector.Start(options =>
            {
                options.ServiceName = "test.service";
                options.EnableConsoleLogging = false;
                options.ConnectionString = LocalConnectionString;
                options.TracesPerSecond = 0.0001;
            });

            var recorded = 0;
            for (var i = 0; i < 25; i++)
            {
                using var activity = pipeline.StartActivity($"test.operation.{i}", ActivityKind.Server);
                if (activity?.Recorded == true)
                {
                    recorded++;
                }
            }

            Assert.True(recorded < 25, "the configured rate limit was ignored");
        }

        [Fact]
        public void ForceFlush_CoversLogsAsWellAsTraces()
        {
            using var pipeline = GraftAppInsightsConnector.Start(options =>
            {
                options.ServiceName = "test.service";
                options.EnableConsoleLogging = false;
                options.ConnectionString = LocalConnectionString;
            });

            using (var activity = pipeline.StartActivity("test.operation"))
            {
                pipeline.Logger.LogInformation("Test log {Value}", 42);
            }
            Assert.True(pipeline.ForceFlush(10000));
        }

        [Fact]
        public void MissingConnectionString_IsReported()
        {
            var previous = Environment.GetEnvironmentVariable("APPLICATIONINSIGHTS_CONNECTION_STRING");

            try
            {
                Environment.SetEnvironmentVariable("APPLICATIONINSIGHTS_CONNECTION_STRING", null);

                using var pipeline = GraftAppInsightsConnector.Start(options =>
                {
                    options.ServiceName = "test.service";
                    options.EnableConsoleLogging = false;
                });

                Assert.False(pipeline.IsExportingToAppInsightsConnector);
            }
            finally
            {
                Environment.SetEnvironmentVariable(
                    "APPLICATIONINSIGHTS_CONNECTION_STRING",
                    previous);
            }
        }

        [Fact]
        public void RequestOperationName_UsesControllerWhenAvailable()
        {
            var context = new DefaultHttpContext();
            context.Request.Method = "GET";
            context.Request.Path = "/api/fetch/Pawel6";
            context.Request.RouteValues["controller"] = "Fetch";

            var name = GraftAppInsightsConnectorRequestTelemetryMiddleware.ResolveOperationName(
                context,
                "TestGgWebApi");

            Assert.Equal("TestGgWebApi.Fetch", name);
        }

        [Fact]
        public void RequestOperationName_FallsBackToMethodAndPath()
        {
            var context = new DefaultHttpContext();
            context.Request.Method = "GET";
            context.Request.Path = "/health";

            var name = GraftAppInsightsConnectorRequestTelemetryMiddleware.ResolveOperationName(
                context,
                "TestGgWebApi");

            Assert.Equal("TestGgWebApi GET /health", name);
        }

        [Fact]
        public void DependencyInjection_RegistersOnePipeline()
        {
            var services = new ServiceCollection();
            services.AddGraftAppInsightsConnector(options =>
            {
                options.ServiceName = "test.service";
                options.EnableConsoleLogging = false;
            });

            using var provider = services.BuildServiceProvider();

            Assert.Same(
                provider.GetRequiredService<GraftAppInsightsConnectorPipeline>(),
                provider.GetRequiredService<GraftAppInsightsConnectorPipeline>());
        }
    }
}
