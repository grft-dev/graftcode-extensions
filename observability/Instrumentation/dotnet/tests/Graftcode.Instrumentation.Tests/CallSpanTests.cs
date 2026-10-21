using System;
using System.Collections.Generic;
using System.Diagnostics;
using Hypertube.Netcore.Utils;
using Hypertube.Netcore.Utils.ConnectionData;
using Hypertube.Netcore.Utils.Logging;
using Xunit;

namespace Graftcode.Instrumentation.Tests
{
    public class CallSpanTests
    {
        private const string TraceId = "0af7651916cd43dd8448eb211c80319c";
        private const string ParentSpanId = "b7ad6b7169203331";
        private const string TraceParent = "00-" + TraceId + "-" + ParentSpanId + "-01";

        [Fact]
        [Trait("Test", "Unit")]
        public void Invoke_OpensADependencySpanNamedAfterTheTarget()
        {
            using var harness = new Harness();

            harness.Complete(harness.OnInvoke(Commands.Fetch(), new InMemoryConnectionData()), Commands.Value("ok"), null);

            var stopped = Assert.Single(harness.Stopped);
            Assert.Equal(GraftcodeInstrumentation.ActivitySourceName, stopped.Source.Name);
            Assert.Equal("Graft SomeLib inmemory", stopped.OperationName);
            Assert.Equal("Graft", stopped.GetTagItem(CallSpan.DependencyType));
            Assert.Equal("SomeLib inmemory", stopped.GetTagItem(CallSpan.DependencyTarget));
            Assert.Equal("SomeLib.Service.Fetch", stopped.GetTagItem("hypertube.operation"));
            Assert.Equal("InvokeStaticMethod", stopped.GetTagItem("hypertube.command_type"));
            Assert.Equal("Netcore", stopped.GetTagItem("hypertube.runtime"));
            Assert.Equal("InMemory", stopped.GetTagItem("hypertube.connection_type"));
            Assert.Equal("inmemory", stopped.GetTagItem("hypertube.target"));
            Assert.Equal(ActivityStatusCode.Ok, stopped.Status);
            Assert.Null(Activity.Current);
        }

        [Fact]
        [Trait("Test", "Unit")]
        public void Invoke_WritesTheSpanIntoTheHeaders_AndRestoresThem()
        {
            using var harness = new Harness();
            var connection = new InMemoryConnectionData();
            connection.SetThreadLocalHeaders(new Dictionary<string, string> { ["tenant"] = "t1" });

            var scope = (CallSpan)harness.OnInvoke(Commands.Fetch(), connection)!;

            Assert.Equal(scope.Activity.Id, connection.GetHeaders()[CallSpan.TraceParentHeader]);
            Assert.Equal(scope.Activity.TraceId.ToHexString(), connection.GetHeaders()[CallSpan.TraceIdHeader]);
            Assert.Equal("t1", connection.GetHeaders()["tenant"]);

            harness.Complete(scope, null, null);

            Assert.False(connection.GetHeaders().ContainsKey(CallSpan.TraceParentHeader));
            Assert.False(connection.GetHeaders().ContainsKey(CallSpan.TraceIdHeader));
            Assert.Equal("t1", connection.GetHeaders()["tenant"]);
        }

        [Fact]
        [Trait("Test", "Unit")]
        public void Invoke_DoesNotReplaceATraceTheCallerAlreadySet()
        {
            using var harness = new Harness();
            var connection = new InMemoryConnectionData();
            connection.SetHeaders(new Dictionary<string, string> { [CallSpan.TraceParentHeader] = TraceParent });

            var scope = (CallSpan)harness.OnInvoke(Commands.Fetch(), connection)!;

            Assert.Equal(TraceId, scope.Activity.TraceId.ToHexString());
            Assert.Equal(ParentSpanId, scope.Activity.ParentSpanId.ToHexString());
            Assert.Equal(TraceParent, connection.GetHeaders()[CallSpan.TraceParentHeader]);
            harness.Complete(scope, null, null);
        }

        [Fact]
        [Trait("Test", "Unit")]
        public void NestedInvoke_OnTheSameConnection_PropagatesTheInnerSpan_AndRestoresTheOuter()
        {
            using var harness = new Harness();
            var connection = new InMemoryConnectionData();

            var outer = (CallSpan)harness.OnInvoke(Commands.Fetch(), connection)!;
            var inner = (CallSpan)harness.OnInvoke(Commands.Fetch(), connection)!;

            Assert.Equal(outer.Activity.SpanId, inner.Activity.ParentSpanId);
            Assert.Equal(inner.Activity.Id, connection.GetHeaders()[CallSpan.TraceParentHeader]);

            harness.Complete(inner, null, null);
            Assert.Equal(outer.Activity.Id, connection.GetHeaders()[CallSpan.TraceParentHeader]);
            Assert.Same(outer.Activity, Activity.Current);

            harness.Complete(outer, null, null);
            Assert.False(connection.GetHeaders().ContainsKey(CallSpan.TraceParentHeader));
            Assert.Null(Activity.Current);
        }

        [Fact]
        [Trait("Test", "Unit")]
        public void WithAutoInjectDisabled_HeadersAreLeftAlone()
        {
            using var harness = new Harness();
            GraftcodeTelemetrySettings.IsAutoInjectEnabled = false;
            var connection = new InMemoryConnectionData();

            var scope = harness.OnInvoke(Commands.Fetch(), connection)!;

            Assert.False(connection.GetHeaders().ContainsKey(CallSpan.TraceParentHeader));
            harness.Complete(scope, null, null);
        }

        [Fact]
        [Trait("Test", "Unit")]
        public void FailedCall_StopsTheSpanWithARedactedMessage()
        {
            using var harness = new Harness();

            harness.Complete(
                harness.OnInvoke(Commands.Fetch(), new InMemoryConnectionData()),
                null,
                new InvalidOperationException("Password=hunter2"));

            var stopped = Assert.Single(harness.Stopped);
            Assert.Equal(ActivityStatusCode.Error, stopped.Status);
            Assert.Equal(LogsRedactor.Placeholder, stopped.StatusDescription);
            Assert.Equal(typeof(InvalidOperationException).FullName, stopped.GetTagItem("exception.type"));
            Assert.Equal(LogsRedactor.Placeholder, stopped.GetTagItem("exception.message"));
        }

        [Theory]
        [Trait("Test", "Unit")]
        [InlineData(CommandType.InvokeDelegate)]
        [InlineData(CommandType.PassByValue)]
        [InlineData(CommandType.RegisterForUpdate)]
        [InlineData(CommandType.ValueForUpdate)]
        public void TransportCommands_DoNotOpenASpan(CommandType type)
        {
            using var harness = new Harness();

            Assert.Null(harness.OnInvoke(Commands.Of(type, "x"), new InMemoryConnectionData()));
            Assert.Null(harness.OnHandle(Commands.Of(type, "x"), Harness.NoHeaders));
            Assert.Empty(harness.Stopped);
        }

        [Fact]
        [Trait("Test", "Unit")]
        public void TelemetrySetupCalls_DoNotOpenASpan()
        {
            using var harness = new Harness();
            var setup = Commands.Of(
                CommandType.InvokeStaticMethod,
                Commands.Of(CommandType.GetType, Commands.Value("App.TelemetrySetup")),
                Commands.Value("Initialize"));

            Assert.Null(harness.OnHandle(setup, Harness.NoHeaders));
        }

        [Fact]
        [Trait("Test", "Unit")]
        public void UnrecordedSpan_GetsNoTags_ButStillPropagates()
        {
            using var harness = new Harness(recording: false);
            var connection = new InMemoryConnectionData();

            var scope = (CallSpan)harness.OnInvoke(Commands.Fetch(), connection)!;

            Assert.False(scope.Activity.IsAllDataRequested);
            Assert.Empty(scope.Activity.TagObjects);
            Assert.Equal(scope.Activity.Id, connection.GetHeaders()[CallSpan.TraceParentHeader]);
            harness.Complete(scope, null, null);
        }

        [Fact]
        [Trait("Test", "Unit")]
        public void Handle_IsNamedAfterTheOperation_AndParentedOnTheIncomingTraceParent()
        {
            using var harness = new Harness();
            var headers = new Dictionary<string, string> { [CallSpan.TraceParentHeader] = TraceParent };

            var scope = (CallSpan)harness.OnHandle(Commands.Fetch(), headers)!;

            Assert.Equal("SomeLib.Service.Fetch", scope.Activity.OperationName);
            Assert.Equal(TraceId, scope.Activity.TraceId.ToHexString());
            Assert.Equal(ParentSpanId, scope.Activity.ParentSpanId.ToHexString());
            harness.Complete(scope, null, null);
        }

        [Fact]
        [Trait("Test", "Unit")]
        public void Handle_FallsBackToTheBareTraceId()
        {
            using var harness = new Harness();
            var headers = new Dictionary<string, string> { [CallSpan.TraceIdHeader] = TraceId };

            var scope = (CallSpan)harness.OnHandle(Commands.Fetch(), headers)!;

            Assert.Equal(TraceId, scope.Activity.TraceId.ToHexString());
            harness.Complete(scope, null, null);
        }

        [Fact]
        [Trait("Test", "Unit")]
        public void LoadedLibrary_NamesTheTargetOfLaterCalls()
        {
            using var harness = new Harness();
            var connection = new InMemoryConnectionData();
            var load = Commands.Of(CommandType.LoadLibrary, Commands.Value("C:\\libs\\TestGgLib.dll"));
            var fetch = Commands.Of(
                CommandType.InvokeStaticMethod,
                Commands.Of(CommandType.GetType, Commands.Value("TestGgLib.RandomService")),
                Commands.Value("Fetch"));

            harness.Complete(harness.OnInvoke(load, connection), null, null);
            harness.Complete(harness.OnInvoke(fetch, connection), null, null);

            Assert.Equal("Graft TestGgLib inmemory", harness.Stopped[1].OperationName);
        }
    }
}
