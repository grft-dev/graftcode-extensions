using System;
using System.Collections.Generic;
using System.Diagnostics;
using Hypertube.Netcore.Utils;
using Hypertube.Netcore.Utils.Abstractions;
using Hypertube.Netcore.Utils.ConnectionData;
using Hypertube.Netcore.Utils.Logging;
using Microsoft.Extensions.Logging;

namespace Graftcode.Instrumentation
{
    internal sealed class GraftcodeInstrumentation
        : IHypertubeInvocation, IHypertubeHandling, IHypertubeLogging, IDisposable
    {
        internal const string ActivitySourceName = "Graftcode";

        internal static readonly ActivitySource Source = new ActivitySource(ActivitySourceName);

        private readonly ILogger? _logger;
        private readonly ActivityListener _propagation = new ActivityListener
        {
            ShouldListenTo = source => source.Name == ActivitySourceName,
            Sample = (ref ActivityCreationOptions<ActivityContext> _) => ActivitySamplingResult.PropagationData,
            SampleUsingParentId = (ref ActivityCreationOptions<string> _) => ActivitySamplingResult.PropagationData,
        };

        internal GraftcodeInstrumentation(ILogger? logger = null)
        {
            _logger = logger;
            ActivitySource.AddActivityListener(_propagation);
            HypertubeBroker.Attach(this);
            HypertubeLogger.Attach(this);
        }

        public void OnExecute(HypertubeCallContext context, Command command, IConnectionData connectionData)
        {
            var reported = LogsCommandResolver.ResolveForInvoke(command);
            var span = CallSpan.StartInvoke(reported, connectionData);
            if (span != null)
            {
                context.Items.Add(span);
            }
        }

        public void OnResponse(HypertubeCallContext context, Command response)
        {
            CompleteSpans(context, response, null);
        }

        public void OnError(HypertubeCallContext context, Exception exception)
        {
            CompleteSpans(context, null, exception);
        }

        public void OnProcess(HypertubeCallContext context, Command command)
        {
            var headers = new Dictionary<string, string>();
            var reported = LogsCommandResolver.ResolveForHandle(command, headers);
            var span = CallSpan.StartHandle(reported, headers);
            if (span != null)
            {
                context.Items.Add(span);
            }
        }

        public void OnProcessed(HypertubeCallContext context, Command response)
        {
            CompleteSpans(context, response, null);
        }

        void IHypertubeHandling.OnError(HypertubeCallContext context, Exception exception)
        {
            CompleteSpans(context, null, exception);
        }

        public bool IsRequested()
        {
            if (_logger == null)
            {
                return Activity.Current?.IsAllDataRequested == true;
            }

            for (var level = LogLevel.Critical; level >= LogLevel.Trace; level--)
            {
                if (_logger.IsEnabled(level))
                {
                    return true;
                }
            }

            return false;
        }

        internal bool IsLogEnabled(LogsLevel level)
        {
            return _logger != null
                ? _logger.IsEnabled(ToLogLevel(level))
                : Activity.Current?.IsAllDataRequested == true;
        }

        public void OnLog(
            LogsLevel level,
            string message,
            IReadOnlyDictionary<string, string> properties,
            Exception? exception)
        {
            if (_logger == null)
            {
                RecordSpanEvent(level, message, properties, exception);
                return;
            }

            if (!_logger.IsEnabled(ToLogLevel(level)))
            {
                return;
            }

            var state = new List<KeyValuePair<string, object?>>(properties.Count + 1);
            foreach (var property in properties)
            {
                state.Add(new KeyValuePair<string, object?>(property.Key, property.Value));
            }

            state.Add(new KeyValuePair<string, object?>("{OriginalFormat}", message));
            _logger.Log(ToLogLevel(level), default, state, exception, (_, __) => message);
        }

        public void Dispose()
        {
            HypertubeBroker.Detach(this);
            HypertubeLogger.Detach(this);
            _propagation.Dispose();
        }

        private static void CompleteSpans(HypertubeCallContext context, Command? response, Exception? exception)
        {
            for (var i = context.Items.Count - 1; i >= 0; i--)
            {
                if (context.Items[i] is CallSpan span)
                {
                    span.Complete(response, exception);
                    context.Items.RemoveAt(i);
                }
            }
        }

        private static void RecordSpanEvent(
            LogsLevel level,
            string message,
            IReadOnlyDictionary<string, string> properties,
            Exception? exception)
        {
            var activity = Activity.Current;
            if (activity == null || !activity.IsAllDataRequested)
            {
                return;
            }

            var tags = new ActivityTagsCollection { { "log.level", ToLogLevel(level).ToString() } };
            foreach (var property in properties)
            {
                tags[property.Key] = property.Value;
            }

            if (exception != null)
            {
                tags["log.message"] = message;
                tags["exception.type"] = exception.GetType().FullName;
                tags["exception.message"] = LogsRedactor.RedactValue(exception.Message);
                tags["exception.stacktrace"] = LogsRedactor.RedactValue(exception.ToString());
            }

            activity.AddEvent(new ActivityEvent(exception == null ? message : "exception", tags: tags));
        }

        private static LogLevel ToLogLevel(LogsLevel level)
        {
            return level == LogsLevel.Off ? LogLevel.None : (LogLevel)(int)level;
        }
    }
}
