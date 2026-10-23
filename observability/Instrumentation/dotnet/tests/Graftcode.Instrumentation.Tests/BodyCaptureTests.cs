using System;
using Hypertube.Netcore.Utils;
using Hypertube.Netcore.Utils.ConnectionData;
using Hypertube.Netcore.Utils.Logging;
using Xunit;

namespace Graftcode.Instrumentation.Tests
{
    public class BodyCaptureTests
    {
        [Fact]
        [Trait("Test", "Unit")]
        public void BodyCapture_IsDisabledByDefault()
        {
            Assert.False(GraftcodeTelemetrySettings.IsBodyCaptureEnabled);
        }

        [Fact]
        [Trait("Test", "Unit")]
        public void BodyLimit_IsClamped()
        {
            Assert.Equal(GraftcodeTelemetrySettings.MinimumBodyMaxChars, GraftcodeTelemetrySettings.ClampBodyMaxCharacters(1));
            Assert.Equal(GraftcodeTelemetrySettings.MaximumBodyMaxChars, GraftcodeTelemetrySettings.ClampBodyMaxCharacters(int.MaxValue));
        }

        [Fact]
        [Trait("Test", "Unit")]
        public void WithCaptureDisabled_SpanCarriesNoBody()
        {
            using var harness = new Harness();

            Invoke(harness, Commands.Fetch(), Commands.Value("ok"));

            var stopped = Assert.Single(harness.Stopped);
            Assert.Null(stopped.GetTagItem(CallSpan.RequestBody));
            Assert.Null(stopped.GetTagItem(CallSpan.ResponseBody));
        }

        [Fact]
        [Trait("Test", "Unit")]
        public void WithCaptureEnabled_RequestAndResponseBodiesAreAttached()
        {
            using var harness = Capturing();

            Invoke(harness, Commands.Fetch(), Commands.Value("order-4711"));

            Assert.Contains("Fetch", Tag(harness, CallSpan.RequestBody));
            Assert.Contains("order-4711", Tag(harness, CallSpan.ResponseBody));
        }

        [Fact]
        [Trait("Test", "Unit")]
        public void WithCaptureEnabled_HandleSpanCarriesTheRequestBody()
        {
            using var harness = Capturing();

            harness.Complete(harness.OnHandle(Commands.Fetch(), Harness.NoHeaders), null, null);

            Assert.Contains("Fetch", Tag(harness, CallSpan.RequestBody));
        }

        [Fact]
        [Trait("Test", "Unit")]
        public void WithCaptureEnabled_SensitiveValuesAreRedacted()
        {
            using var harness = Capturing();

            Invoke(harness, Commands.Of(CommandType.InvokeStaticMethod, "password", "hunter2", "note", "Server=x;Password=s3cr3t"), null);

            var body = Tag(harness, CallSpan.RequestBody);
            Assert.Contains(LogsRedactor.Placeholder, body);
            Assert.DoesNotContain("hunter2", body);
            Assert.DoesNotContain("s3cr3t", body);
        }

        [Fact]
        [Trait("Test", "Unit")]
        public void OversizedPayload_SetsTheTruncationFlag()
        {
            using var harness = Capturing(256);
            var payload = new object[64];
            for (var i = 0; i < payload.Length; i++)
            {
                payload[i] = new string('y', 64);
            }

            Invoke(harness, Commands.Of(CommandType.InvokeStaticMethod, payload), null);

            Assert.Equal(true, harness.Stopped[0].GetTagItem(CallSpan.RequestBodyTruncated));
            Assert.Contains(BodyCapture.TruncationMarker, Tag(harness, CallSpan.RequestBody));
        }

        [Fact]
        [Trait("Test", "Unit")]
        public void UnrecordedSpan_SkipsBodyRendering()
        {
            using var harness = new Harness(recording: false);
            GraftcodeTelemetrySettings.IsBodyCaptureEnabled = true;

            var scope = (CallSpan)harness.OnInvoke(Commands.Fetch(), new InMemoryConnectionData())!;
            harness.Complete(scope, Commands.Value("ok"), null);

            Assert.Empty(scope.Activity.TagObjects);
        }

        [Fact]
        [Trait("Test", "Unit")]
        public void PayloadWithAThrowingToString_IsRenderedAsAPlaceholder()
        {
            using var harness = Capturing();

            Invoke(harness, Commands.Of(CommandType.InvokeStaticMethod, "Fetch", new HostileDto()), null);

            var body = Tag(harness, CallSpan.RequestBody);
            Assert.Contains("Fetch", body);
            Assert.Contains("[unrenderable:" + typeof(HostileDto).FullName + "]", body);
        }

        [Fact]
        [Trait("Test", "Unit")]
        public void ByteArrays_AreRenderedAsTheirLength()
        {
            using var harness = Capturing();

            Invoke(harness, Commands.Of(CommandType.InvokeStaticMethod, new byte[42]), null);

            Assert.Contains("[bytes:42]", Tag(harness, CallSpan.RequestBody));
        }

        private sealed class HostileDto
        {
            public override string ToString() => throw new InvalidOperationException("ToString exploded");
        }

        private static Harness Capturing(int? maxCharacters = null)
        {
            var harness = new Harness();
            GraftcodeTelemetrySettings.IsBodyCaptureEnabled = true;
            if (maxCharacters.HasValue)
            {
                GraftcodeTelemetrySettings.BodyMaxCharacters = maxCharacters.Value;
            }

            return harness;
        }

        private static void Invoke(Harness harness, ICommand command, ICommand? response)
        {
            var span = harness.OnInvoke(command, new InMemoryConnectionData());
            harness.Complete(span, response, null);
        }

        private static string Tag(Harness harness, string key)
        {
            return harness.Stopped[0].GetTagItem(key) as string ?? string.Empty;
        }
    }
}
