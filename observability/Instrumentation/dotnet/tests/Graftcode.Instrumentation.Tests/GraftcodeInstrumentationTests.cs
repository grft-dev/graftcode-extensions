using System;
using System.Collections.Generic;
using System.Linq;
using Hypertube.Netcore.Utils.Logging;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using OpenTelemetry;
using OpenTelemetry.Trace;
using Xunit;

namespace Graftcode.Instrumentation.Tests
{
    public class GraftcodeInstrumentationTests : IDisposable
    {
        private readonly LogsLevel _minimumLevel = HypertubeLogger.MinimumLevel;

        public GraftcodeInstrumentationTests() => HypertubeLogger.MinimumLevel = LogsLevel.Off;

        public void Dispose() => HypertubeLogger.MinimumLevel = _minimumLevel;

        [Fact]
        [Trait("Test", "Unit")]
        public void Construction_AttachesToHypertube_AndDisposeDetaches()
        {
            HypertubeLogger.MinimumLevel = LogsLevel.Error;
            var listener = new GraftcodeInstrumentation(new RecordingLogger());
            Assert.True(HypertubeLogger.IsEnabled(LogsLevel.Info));

            listener.Dispose();
            Assert.False(HypertubeLogger.IsEnabled(LogsLevel.Info));
        }

        [Fact]
        [Trait("Test", "Unit")]
        public void WithoutALogger_LogsBecomeSpanEvents_WithTheExceptionRedacted()
        {
            using var harness = new Harness();

            var scope = harness.OnHandle(Commands.Fetch(), Harness.NoHeaders)!;
            Assert.True(harness.Listener.IsLogEnabled(LogsLevel.Info));
            harness.Listener.OnLog(
                LogsLevel.Info,
                "Hypertube handling InvokeStaticMethod",
                new Dictionary<string, string> { ["CommandType"] = "InvokeStaticMethod" },
                null);
            harness.Listener.OnLog(LogsLevel.Error, "failed", Harness.NoHeaders, new InvalidOperationException("Password=hunter2"));
            harness.Complete(scope, null, null);

            var events = Assert.Single(harness.Stopped).Events.ToList();
            Assert.Equal("Hypertube handling InvokeStaticMethod", events[0].Name);
            Assert.Equal("InvokeStaticMethod", events[0].Tags.Single(tag => tag.Key == "CommandType").Value);
            Assert.Equal("Information", events[0].Tags.Single(tag => tag.Key == "log.level").Value);
            Assert.Equal("exception", events[1].Name);
            Assert.Equal(LogsRedactor.Placeholder, events[1].Tags.Single(tag => tag.Key == "exception.message").Value);
            Assert.All(events[1].Tags, tag => Assert.DoesNotContain("hunter2", tag.Value?.ToString()));
        }

        [Fact]
        [Trait("Test", "Unit")]
        public void WithALogger_LogsAreForwarded_InsteadOfRecordedOnTheSpan()
        {
            var logger = new RecordingLogger();
            using var harness = new Harness(logger);

            var scope = (CallSpan)harness.OnHandle(Commands.Fetch(), Harness.NoHeaders)!;
            harness.Listener.OnLog(LogsLevel.Warning, "hello ada", new Dictionary<string, string> { ["Name"] = "ada" }, null);
            harness.Complete(scope, null, null);

            var entry = Assert.Single(logger.Entries);
            Assert.Equal(LogLevel.Warning, entry.Level);
            Assert.Equal("hello ada", entry.Message);
            Assert.Equal(scope.Activity.SpanId, entry.SpanId);
            Assert.Contains(entry.State, pair => pair.Key == "Name" && (string?)pair.Value == "ada");
            Assert.Contains(entry.State, pair => pair.Key == "{OriginalFormat}" && (string?)pair.Value == "hello ada");
            Assert.Empty(Assert.Single(harness.Stopped).Events);
        }

        [Fact]
        [Trait("Test", "Unit")]
        public void LoggerAtInformation_ReceivesHypertubeLines_AtItsLevelOnly()
        {
            var logger = new RecordingLogger(LogLevel.Information);
            using var listener = new GraftcodeInstrumentation(logger);

            HypertubeLogger.GetLogger().Log(LogsLevel.Debug, "Hypertube debug {Detail}", "x");
            HypertubeLogger.GetLogger().Log(LogsLevel.Info, "Hypertube invoking {CommandType}", "InvokeStaticMethod");

            var entry = Assert.Single(logger.Entries);
            Assert.Equal(LogLevel.Information, entry.Level);
            Assert.Equal("Hypertube invoking InvokeStaticMethod", entry.Message);
        }

        [Fact]
        [Trait("Test", "Unit")]
        public void LoggerLevel_DecidesWhatIsEnabled()
        {
            using var harness = new Harness(new RecordingLogger(LogLevel.Error));

            Assert.False(harness.Listener.IsLogEnabled(LogsLevel.Warning));
            Assert.True(harness.Listener.IsLogEnabled(LogsLevel.Error));
        }

        [Fact]
        [Trait("Test", "Unit")]
        public void WithoutAnExporter_LogsAreDisabled()
        {
            using var harness = new Harness(recording: false);

            var scope = harness.OnHandle(Commands.Fetch(), Harness.NoHeaders)!;

            Assert.False(harness.Listener.IsLogEnabled(LogsLevel.Error));
            harness.Complete(scope, null, null);
        }

        [Fact]
        [Trait("Test", "Unit")]
        public void Builder_AttachesForTheProviderLifetime()
        {
            HypertubeLogger.MinimumLevel = LogsLevel.Error;

            using (Sdk.CreateTracerProviderBuilder().AddGraftcodeInstrumentation(new RecordingLogger()).Build())
            {
                Assert.True(HypertubeLogger.IsEnabled(LogsLevel.Info));
            }

            Assert.False(HypertubeLogger.IsEnabled(LogsLevel.Info));

            using (Sdk.CreateTracerProviderBuilder().AddGraftcodeInstrumentation(NullLogger.Instance).Build())
            {
                Assert.False(HypertubeLogger.IsEnabled(LogsLevel.Info));
            }
        }

        [Fact]
        [Trait("Test", "Unit")]
        public void DisposingAReplacedListener_KeepsTheNewerOneAttached()
        {
            var older = new GraftcodeInstrumentation(new RecordingLogger());
            using var newer = new GraftcodeInstrumentation(new RecordingLogger(LogLevel.Error));

            older.Dispose();

            Assert.False(newer.IsLogEnabled(LogsLevel.Warning));
            Assert.True(newer.IsLogEnabled(LogsLevel.Error));
        }
    }
}
