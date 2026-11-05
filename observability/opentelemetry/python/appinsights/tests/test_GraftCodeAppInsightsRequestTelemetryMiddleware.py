import asyncio

import pytest
from opentelemetry import trace
from opentelemetry.trace import SpanKind, StatusCode

from hypertube.utils.logging.LogsLevel import LogsLevel

from graftcode_telemetry_appinsights import GraftCodeAppInsightsRequestTelemetryMiddleware

from InMemoryTelemetry import InMemoryTelemetry

TRACE_ID = "0af7651916cd43dd8448eb211c80319c"
PARENT_SPAN_ID = "b7ad6b7169203331"


class FetchController:
    @staticmethod
    def fetch(input):
        return input


def http_scope(path, headers=None):
    return {
        "type": "http",
        "method": "GET",
        "path": path,
        "root_path": "",
        "headers": [(key.encode("latin-1"), value.encode("latin-1")) for key, value in (headers or {}).items()],
    }


def run(middleware, scope):
    async def receive():
        return {"type": "http.request", "body": b"", "more_body": False}

    async def send(message):
        pass

    asyncio.run(middleware(scope, receive, send))


def responding(status, inside=None):
    async def app(scope, receive, send):
        if inside is not None:
            inside(scope)
        await send({"type": "http.response.start", "status": status, "headers": []})
        await send({"type": "http.response.body", "body": b""})

    return app


def test_request_operation_name_uses_controller_when_available():
    assert GraftCodeAppInsightsRequestTelemetryMiddleware.resolve_operation_name(
        "TestGgPythonWebApi", "GET", "/api/fetch/Pawel6", "Fetch") == "TestGgPythonWebApi.Fetch"


def test_request_operation_name_falls_back_to_method_and_path():
    assert GraftCodeAppInsightsRequestTelemetryMiddleware.resolve_operation_name(
        "TestGgPythonWebApi", "GET", "/health", None) == "TestGgPythonWebApi GET /health"


def test_routed_endpoint_names_the_controller_without_its_suffix():
    scope = http_scope("/api/fetch/x")
    scope["endpoint"] = FetchController.fetch

    assert GraftCodeAppInsightsRequestTelemetryMiddleware.resolve_controller_name(scope) == "Fetch"


def test_request_opens_a_server_span_parented_on_the_incoming_trace_parent_and_current_during_the_app():
    with InMemoryTelemetry(LogsLevel.Info) as telemetry:
        trace_id_inside_app = {}

        def inside(scope):
            trace_id_inside_app["value"] = trace.format_trace_id(trace.get_current_span().get_span_context().trace_id)
            scope["endpoint"] = FetchController.fetch

        middleware = GraftCodeAppInsightsRequestTelemetryMiddleware(responding(200, inside), telemetry.pipeline)
        run(middleware, http_scope("/api/fetch/Pawel6", {"traceparent": "00-" + TRACE_ID + "-" + PARENT_SPAN_ID + "-01"}))

        span = telemetry.spans()[0]
        assert span.name == "TestService.Fetch"
        assert span.kind == SpanKind.SERVER
        assert trace.format_trace_id(span.context.trace_id) == TRACE_ID
        assert trace.format_span_id(span.parent.span_id) == PARENT_SPAN_ID
        assert trace_id_inside_app["value"] == TRACE_ID
        assert span.attributes["http.method"] == "GET"
        assert span.attributes["http.target"] == "/api/fetch/Pawel6"
        assert "http.route" not in span.attributes
        assert span.attributes["http.status_code"] == 200
        assert not trace.get_current_span().get_span_context().is_valid


def test_server_errors_mark_the_span_as_failed():
    with InMemoryTelemetry(LogsLevel.Info) as telemetry:
        run(GraftCodeAppInsightsRequestTelemetryMiddleware(responding(503), telemetry.pipeline), http_scope("/boom"))

        span = telemetry.spans()[0]
        assert span.name == "TestService GET /boom"
        assert span.status.status_code == StatusCode.ERROR


def test_exceptions_are_recorded_and_rethrown():
    async def failing(scope, receive, send):
        raise RuntimeError("boom")

    with InMemoryTelemetry(LogsLevel.Info) as telemetry:
        with pytest.raises(RuntimeError):
            run(GraftCodeAppInsightsRequestTelemetryMiddleware(failing, telemetry.pipeline), http_scope("/boom"))

        span = telemetry.spans()[0]
        assert span.status.status_code == StatusCode.ERROR
        assert span.events[0].name == "exception"


def test_non_http_scopes_pass_through():
    calls = []

    async def app(scope, receive, send):
        calls.append(scope["type"])

    with InMemoryTelemetry(LogsLevel.Info) as telemetry:
        run(GraftCodeAppInsightsRequestTelemetryMiddleware(app, telemetry.pipeline), {"type": "lifespan"})

        assert calls == ["lifespan"]
        assert telemetry.spans() == []
