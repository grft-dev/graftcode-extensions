import random

from opentelemetry import context as otel_context
from opentelemetry import trace
from opentelemetry.context import Context
from opentelemetry.propagators.textmap import Getter
from opentelemetry.trace import NonRecordingSpan, SpanContext, SpanKind, Status, StatusCode, TraceFlags
from opentelemetry.trace.propagation.tracecontext import TraceContextTextMapPropagator

from hypertube.utils.CommandType import CommandType
from hypertube.utils.logging.LogsRedactor import LogsRedactor

from graftcode_instrumentation.BodyCapture import BodyCapture
from graftcode_instrumentation.GraftcodeTelemetrySettings import GraftcodeTelemetrySettings
from graftcode_instrumentation.LibraryCatalog import LibraryCatalog
from graftcode_instrumentation.OperationName import OperationName

_SKIPPED_COMMANDS = (
    CommandType.PassByValue,
    CommandType.RegisterForUpdate,
    CommandType.ValueForUpdate,
    CommandType.InvokeDelegate,
)


def _header(headers, key):
    value = headers.get(key)
    if value is not None:
        return value

    for name, candidate in headers.items():
        if key.lower() == str(name).lower():
            return candidate

    return None


class _HeaderGetter(Getter):

    def get(self, carrier, key):
        value = _header(carrier, key)
        return None if value is None else [value]

    def keys(self, carrier):
        return list(carrier.keys())


class CallSpan:
    DEPENDENCY_TYPE = "graft.dependency.type"
    DEPENDENCY_TARGET = "graft.dependency.target"
    BRIDGE_DEPENDENCY_TYPE = "Graft"
    REQUEST_BODY = "hypertube.request.body"
    RESPONSE_BODY = "hypertube.response.body"
    REQUEST_BODY_TRUNCATED = "hypertube.request.body.truncated"
    RESPONSE_BODY_TRUNCATED = "hypertube.response.body.truncated"
    TRACE_PARENT_HEADER = "traceparent"
    TRACE_STATE_HEADER = "tracestate"
    TRACE_ID_HEADER = "traceid"

    _PROPAGATOR = TraceContextTextMapPropagator()
    _HEADER_GETTER = _HeaderGetter()

    def __init__(self, span):
        self.span = span
        self._token = otel_context.attach(trace.set_span_in_context(span))
        self._injected_into = None
        self._previous_headers = None
        self._completed = False

    @staticmethod
    def start_invoke(tracer, command, connection_data):
        operation = CallSpan._resolve_operation(command)
        if operation is None:
            return None

        LibraryCatalog.record(command, connection_data)
        target = LibraryCatalog.describe_target(command, connection_data)
        span = CallSpan._start(tracer, "Graft " + target, connection_data.get_headers())

        if span.is_recording():
            span.set_attribute("hypertube.runtime", str(command.runtime_name))
            span.set_attribute("hypertube.command_type", command.command_type.name)
            span.set_attribute("hypertube.connection_type", connection_data.connection_type.name)
            span.set_attribute(CallSpan.DEPENDENCY_TYPE, CallSpan.BRIDGE_DEPENDENCY_TYPE)
            CallSpan._set_if_present(span, CallSpan.DEPENDENCY_TARGET, LogsRedactor.redact_value(target))
            CallSpan._set_if_present(span, "hypertube.operation", LogsRedactor.redact_value(operation))
            CallSpan._set_if_present(span, "hypertube.target",
                                     LogsRedactor.redact_value(LogsRedactor.redact_target(connection_data.hostname)))

        call_span = CallSpan(span)
        call_span._capture(command, CallSpan.REQUEST_BODY, CallSpan.REQUEST_BODY_TRUNCATED)
        if GraftcodeTelemetrySettings.is_auto_inject_enabled():
            call_span._inject_into(connection_data)

        return call_span

    @staticmethod
    def start_handle(tracer, command, headers):
        operation = CallSpan._resolve_operation(command)
        if operation is None:
            return None

        call_span = CallSpan(CallSpan._start(tracer, operation, headers))
        call_span._capture(command, CallSpan.REQUEST_BODY, CallSpan.REQUEST_BODY_TRUNCATED)
        return call_span

    def complete(self, response, exception):
        if self._completed:
            return

        self._completed = True
        try:
            if response is not None:
                self._capture(response, CallSpan.RESPONSE_BODY, CallSpan.RESPONSE_BODY_TRUNCATED)

            if exception is None:
                self.span.set_status(Status(StatusCode.OK))
            else:
                message = LogsRedactor.redact_value(str(exception))
                self.span.set_attribute("exception.type", OperationName._full_name(type(exception)))
                CallSpan._set_if_present(self.span, "exception.message", message)
                self.span.set_status(Status(StatusCode.ERROR, message or ""))

            if self._injected_into is not None:
                self._injected_into.set_thread_local_headers(self._previous_headers)
        finally:
            try:
                otel_context.detach(self._token)
            finally:
                self.span.end()

    def dispose(self):
        self.complete(None, None)

    def close(self):
        self.complete(None, None)

    @staticmethod
    def _resolve_operation(command):
        if command.command_type in _SKIPPED_COMMANDS:
            return None

        operation = OperationName.resolve(command)
        return None if operation.lower().endswith("telemetrysetup.initialize") else operation

    @staticmethod
    def _start(tracer, name, headers):
        carrier = headers if headers is not None else {}

        remote = CallSpan._PROPAGATOR.extract(carrier, context=Context(), getter=CallSpan._HEADER_GETTER)
        if trace.get_current_span(remote).get_span_context().is_valid:
            return tracer.start_span(name, context=remote, kind=SpanKind.INTERNAL)

        trace_id = CallSpan._read_trace_id(_header(carrier, CallSpan.TRACE_ID_HEADER))
        if trace_id is not None and trace_id != trace.get_current_span().get_span_context().trace_id:
            parent = SpanContext(
                trace_id=trace_id,
                span_id=CallSpan._random_span_id(),
                is_remote=True,
                trace_flags=TraceFlags(TraceFlags.SAMPLED))
            parent_context = trace.set_span_in_context(NonRecordingSpan(parent), Context())
            return tracer.start_span(name, context=parent_context, kind=SpanKind.INTERNAL)

        return tracer.start_span(name, kind=SpanKind.INTERNAL)

    @staticmethod
    def _read_trace_id(raw):
        if raw is None:
            return None

        hex_value = str(raw).strip().replace("-", "").lower()
        if len(hex_value) != 32 or not hex_value.replace("0", ""):
            return None

        if any(character not in "0123456789abcdef" for character in hex_value):
            return None

        return int(hex_value, 16)

    @staticmethod
    def _random_span_id():
        span_id = 0
        while span_id == 0:
            span_id = random.getrandbits(64)
        return span_id

    def _inject_into(self, connection_data):
        local = connection_data.get_thread_local_headers()
        span_context = self.span.get_span_context()
        if (not span_context.is_valid
                or (CallSpan.TRACE_PARENT_HEADER in connection_data.get_headers()
                    and CallSpan.TRACE_PARENT_HEADER not in local)):
            return

        headers = dict(local)
        headers.pop(CallSpan.TRACE_STATE_HEADER, None)
        CallSpan._PROPAGATOR.inject(headers, context=trace.set_span_in_context(self.span, Context()))
        headers[CallSpan.TRACE_ID_HEADER] = trace.format_trace_id(span_context.trace_id)

        connection_data.set_thread_local_headers(headers)
        self._injected_into = connection_data
        self._previous_headers = local

    def _capture(self, command, body_key, truncated_key):
        if not self.span.is_recording() or not GraftcodeTelemetrySettings.is_body_capture_enabled():
            return

        result = BodyCapture.render(command, GraftcodeTelemetrySettings.get_body_max_characters())
        if result is None or result.body is None:
            return

        self.span.set_attribute(body_key, result.body)
        if result.truncated:
            self.span.set_attribute(truncated_key, True)

    @staticmethod
    def _set_if_present(span, key, value):
        if value is not None:
            span.set_attribute(key, value)
