using System;
using System.Collections.Generic;
using System.Diagnostics;
using Hypertube.Netcore.Utils;
using Hypertube.Netcore.Utils.Abstractions;
using Hypertube.Netcore.Utils.ConnectionData;
using Hypertube.Netcore.Utils.Logging;
using Microsoft.Extensions.Logging;

namespace Graftcode.Instrumentation.Tests
{
    internal static class Commands
    {
        internal static Command Value(object value) => new Command(RuntimeName.Netcore, CommandType.Value, value);

        internal static Command Of(CommandType type, params object[] payload) => new Command(RuntimeName.Netcore, type, payload);

        internal static Command Fetch() =>
            Of(CommandType.InvokeStaticMethod, Of(CommandType.GetType, Value("SomeLib.Service")), Value("Fetch"));
    }

    internal sealed class Harness : IDisposable
    {
        internal static readonly IReadOnlyDictionary<string, string> NoHeaders = new Dictionary<string, string>();

        private readonly ActivityListener? _exporter;
        private readonly bool _autoInject = GraftcodeTelemetrySettings.IsAutoInjectEnabled;
        private readonly bool _captureBodies = GraftcodeTelemetrySettings.IsBodyCaptureEnabled;
        private readonly int _bodyMaxCharacters = GraftcodeTelemetrySettings.BodyMaxCharacters;
        private readonly Dictionary<CallSpan, (HypertubeCallContext Context, bool Invoke)> _openSpans = new();

        internal Harness(ILogger? logger = null, bool recording = true)
        {
            Activity.Current = null;
            if (recording)
            {
                _exporter = new ActivityListener
                {
                    ShouldListenTo = source => source.Name == GraftcodeInstrumentation.ActivitySourceName,
                    Sample = (ref ActivityCreationOptions<ActivityContext> _) => ActivitySamplingResult.AllDataAndRecorded,
                    ActivityStopped = Stopped.Add,
                };
                ActivitySource.AddActivityListener(_exporter);
            }

            Listener = new GraftcodeInstrumentation(logger);
        }

        internal GraftcodeInstrumentation Listener { get; }

        internal List<Activity> Stopped { get; } = new List<Activity>();

        internal CallSpan? OnInvoke(ICommand command, IConnectionData connection)
        {
            var context = new HypertubeCallContext();
            Listener.OnExecute(context, (Command)command, connection);
            return Track(context, invoke: true);
        }

        internal CallSpan? OnHandle(ICommand command, IReadOnlyDictionary<string, string> headers)
        {
            var context = new HypertubeCallContext();
            var resolved = new Dictionary<string, string>(headers);
            var reported = LogsCommandResolver.ResolveForHandle((Command)command, resolved);
            var span = CallSpan.StartHandle(reported, resolved);
            if (span != null)
            {
                context.Items.Add(span);
            }

            return Track(context, invoke: false);
        }

        internal void Complete(CallSpan? span, ICommand? response, Exception? exception)
        {
            if (span == null || !_openSpans.TryGetValue(span, out var pair))
            {
                return;
            }

            var commandResponse = (Command?)(response ?? Commands.Value(null));
            if (pair.Invoke)
            {
                if (exception != null)
                {
                    Listener.OnError(pair.Context, exception);
                }
                else
                {
                    Listener.OnResponse(pair.Context, commandResponse!);
                }
            }
            else if (exception != null)
            {
                ((IHypertubeHandling)Listener).OnError(pair.Context, exception);
            }
            else
            {
                Listener.OnProcessed(pair.Context, commandResponse!);
            }

            _openSpans.Remove(span);
        }

        public void Dispose()
        {
            Listener.Dispose();
            _exporter?.Dispose();
            GraftcodeTelemetrySettings.IsAutoInjectEnabled = _autoInject;
            GraftcodeTelemetrySettings.IsBodyCaptureEnabled = _captureBodies;
            GraftcodeTelemetrySettings.BodyMaxCharacters = _bodyMaxCharacters;
            Activity.Current = null;
        }

        private CallSpan? Track(HypertubeCallContext context, bool invoke)
        {
            var span = FindSpan(context);
            if (span != null)
            {
                _openSpans[span] = (context, invoke);
            }

            return span;
        }

        private static CallSpan? FindSpan(HypertubeCallContext context)
        {
            for (var i = context.Items.Count - 1; i >= 0; i--)
            {
                if (context.Items[i] is CallSpan span)
                {
                    return span;
                }
            }

            return null;
        }
    }

    internal sealed class RecordingLogger : ILogger
    {
        private readonly LogLevel _minimum;

        internal RecordingLogger(LogLevel minimum = LogLevel.Trace) => _minimum = minimum;

        internal List<(LogLevel Level, string Message, ActivitySpanId SpanId, IReadOnlyList<KeyValuePair<string, object?>> State)> Entries { get; } =
            new List<(LogLevel, string, ActivitySpanId, IReadOnlyList<KeyValuePair<string, object?>>)>();

        public IDisposable? BeginScope<TState>(TState state) where TState : notnull => null;

        public bool IsEnabled(LogLevel logLevel) => logLevel >= _minimum;

        public void Log<TState>(LogLevel logLevel, EventId eventId, TState state, Exception? exception, Func<TState, Exception?, string> formatter)
        {
            Entries.Add((
                logLevel,
                formatter(state, exception),
                Activity.Current?.SpanId ?? default,
                (IReadOnlyList<KeyValuePair<string, object?>>)state!));
        }
    }
}
