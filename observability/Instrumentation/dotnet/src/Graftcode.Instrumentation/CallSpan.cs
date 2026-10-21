using System;
using System.Collections.Generic;
using System.Diagnostics;
using Hypertube.Netcore.Utils;
using Hypertube.Netcore.Utils.ConnectionData;
using Hypertube.Netcore.Utils.Logging;

namespace Graftcode.Instrumentation
{
    internal sealed class CallSpan
    {
        internal const string DependencyType = "graft.dependency.type";
        internal const string DependencyTarget = "graft.dependency.target";
        internal const string BridgeDependencyType = "Graft";
        internal const string RequestBody = "hypertube.request.body";
        internal const string ResponseBody = "hypertube.response.body";
        internal const string RequestBodyTruncated = "hypertube.request.body.truncated";
        internal const string ResponseBodyTruncated = "hypertube.response.body.truncated";
        internal const string TraceParentHeader = "traceparent";
        internal const string TraceStateHeader = "tracestate";
        internal const string TraceIdHeader = "traceid";

        private readonly Activity _activity;
        private IConnectionData? _injectedInto;
        private IDictionary<string, string>? _previousHeaders;

        private CallSpan(Activity activity)
        {
            _activity = activity;
        }

        internal Activity Activity => _activity;

        internal static CallSpan? StartInvoke(ICommand command, IConnectionData connectionData)
        {
            if (!TryResolveOperation(command, out var operation))
            {
                return null;
            }

            LibraryCatalog.Record(command, connectionData);
            var target = LibraryCatalog.DescribeTarget(command, connectionData);
            var activity = Start("Graft " + target, connectionData.GetHeaders());
            if (activity == null)
            {
                return null;
            }

            if (activity.IsAllDataRequested)
            {
                activity.SetTag("hypertube.runtime", command.RuntimeName.ToString());
                activity.SetTag("hypertube.command_type", command.CommandType.ToString());
                activity.SetTag("hypertube.connection_type", connectionData.ConnectionType.ToString());
                activity.SetTag(DependencyType, BridgeDependencyType);
                activity.SetTag(DependencyTarget, LogsRedactor.RedactValue(target));
                activity.SetTag("hypertube.operation", LogsRedactor.RedactValue(operation));
                activity.SetTag("hypertube.target", LogsRedactor.RedactValue(LogsRedactor.RedactTarget(connectionData.Hostname)));
            }

            var span = new CallSpan(activity);
            span.Capture(command, RequestBody, RequestBodyTruncated);
            if (GraftcodeTelemetrySettings.IsAutoInjectEnabled)
            {
                span.InjectInto(connectionData);
            }

            return span;
        }

        internal static CallSpan? StartHandle(ICommand command, IReadOnlyDictionary<string, string> headers)
        {
            if (!TryResolveOperation(command, out var operation))
            {
                return null;
            }

            var activity = Start(operation, headers);
            if (activity == null)
            {
                return null;
            }

            var span = new CallSpan(activity);
            span.Capture(command, RequestBody, RequestBodyTruncated);
            return span;
        }

        public void Complete(ICommand? response, Exception? exception)
        {
            if (response != null)
            {
                Capture(response, ResponseBody, ResponseBodyTruncated);
            }

            if (exception == null)
            {
                _activity.SetStatus(ActivityStatusCode.Ok);
            }
            else
            {
                var message = LogsRedactor.RedactValue(exception.Message);
                _activity.SetTag("exception.type", exception.GetType().FullName);
                _activity.SetTag("exception.message", message);
                _activity.SetStatus(ActivityStatusCode.Error, message);
            }

            _injectedInto?.SetThreadLocalHeaders(_previousHeaders!);
            _activity.Dispose();
        }

        private static bool TryResolveOperation(ICommand command, out string operation)
        {
            operation = string.Empty;
            if (command.CommandType == CommandType.PassByValue
                || command.CommandType == CommandType.RegisterForUpdate
                || command.CommandType == CommandType.ValueForUpdate
                || command.CommandType == CommandType.InvokeDelegate)
            {
                return false;
            }

            operation = OperationName.Resolve(command);
            return !operation.EndsWith("TelemetrySetup.Initialize", StringComparison.OrdinalIgnoreCase);
        }

        private static Activity? Start(string name, IReadOnlyDictionary<string, string> headers)
        {
            var source = GraftcodeInstrumentation.Source;
            headers.TryGetValue(TraceParentHeader, out var traceParent);
            headers.TryGetValue(TraceStateHeader, out var traceState);
            if (ActivityContext.TryParse(traceParent, traceState, out var parent))
            {
                return source.StartActivity(name, ActivityKind.Internal, parent);
            }

            if (headers.TryGetValue(TraceIdHeader, out var rawTraceId)
                && TryReadTraceId(rawTraceId, out var traceId)
                && Activity.Current?.TraceId != traceId)
            {
                return source.StartActivity(name, ActivityKind.Internal, new ActivityContext(traceId, default, ActivityTraceFlags.Recorded));
            }

            return source.StartActivity(name, ActivityKind.Internal);
        }

        private static bool TryReadTraceId(string? raw, out ActivityTraceId traceId)
        {
            traceId = default;
            var hex = raw?.Trim().Replace("-", string.Empty).ToLowerInvariant();
            if (hex == null || hex.Length != 32 || hex.Trim('0').Length == 0)
            {
                return false;
            }

            foreach (var character in hex)
            {
                if ((character < '0' || character > '9') && (character < 'a' || character > 'f'))
                {
                    return false;
                }
            }

            traceId = ActivityTraceId.CreateFromString(hex.AsSpan());
            return true;
        }

        private void InjectInto(IConnectionData connectionData)
        {
            var local = connectionData.GetThreadLocalHeaders();
            if (_activity.IdFormat != ActivityIdFormat.W3C
                || (connectionData.GetHeaders().ContainsKey(TraceParentHeader) && !local.ContainsKey(TraceParentHeader)))
            {
                return;
            }

            var headers = new Dictionary<string, string>(local)
            {
                [TraceParentHeader] = _activity.Id!,
                [TraceIdHeader] = _activity.TraceId.ToHexString(),
            };
            if (!string.IsNullOrEmpty(_activity.TraceStateString))
            {
                headers[TraceStateHeader] = _activity.TraceStateString!;
            }

            connectionData.SetThreadLocalHeaders(headers);
            _injectedInto = connectionData;
            _previousHeaders = local;
        }

        private void Capture(ICommand command, string bodyKey, string truncatedKey)
        {
            if (!_activity.IsAllDataRequested || !GraftcodeTelemetrySettings.IsBodyCaptureEnabled)
            {
                return;
            }

            var body = BodyCapture.Render(command, GraftcodeTelemetrySettings.BodyMaxCharacters, out var truncated);
            if (body == null)
            {
                return;
            }

            _activity.SetTag(bodyKey, body);
            if (truncated)
            {
                _activity.SetTag(truncatedKey, true);
            }
        }
    }
}
