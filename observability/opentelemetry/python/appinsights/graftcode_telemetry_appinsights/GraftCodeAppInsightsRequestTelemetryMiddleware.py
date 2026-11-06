from __future__ import annotations

from collections.abc import Awaitable, Callable, MutableMapping

from opentelemetry import context as otel_context
from opentelemetry import trace
from opentelemetry.trace import SpanKind, Status, StatusCode
from opentelemetry.trace.propagation.tracecontext import TraceContextTextMapPropagator

_CONTROLLER_SUFFIX = "Controller"
_PROPAGATOR = TraceContextTextMapPropagator()

Scope = MutableMapping[str, object]
Message = MutableMapping[str, object]
Receive = Callable[[], Awaitable[dict]]
Send = Callable[[Message], Awaitable[None]]
ASGIApp = Callable[[Scope, Receive, Send], Awaitable[None]]


class GraftCodeAppInsightsRequestTelemetryMiddleware:
    CONTROLLER_ATTRIBUTE = "graft.controller"

    def __init__(self, app: ASGIApp, telemetry: object) -> None:
        if telemetry is None:
            raise ValueError("telemetry must not be None")

        self._app = app
        self._telemetry = telemetry

    async def __call__(self, scope: Scope, receive: Receive, send: Send) -> None:
        if scope.get("type") != "http":
            await self._app(scope, receive, send)
            return

        method = str(scope.get("method", "GET"))
        path = GraftCodeAppInsightsRequestTelemetryMiddleware._resolve_path(scope)
        headers = {
            name.decode("latin-1").lower(): value.decode("latin-1")
            for name, value in scope.get("headers") or []
        }

        parent = _PROPAGATOR.extract(headers, context=otel_context.get_current())
        span = self._telemetry.tracer.start_span(
            GraftCodeAppInsightsRequestTelemetryMiddleware.resolve_operation_name(
                self._telemetry.service_name, method, path, None),
            context=parent,
            kind=SpanKind.SERVER)

        if span.is_recording():
            span.set_attribute("http.method", method)
            span.set_attribute("http.target", path)

        status: dict[str, object | None] = {"code": None}

        async def send_with_status(message: Message) -> None:
            if message.get("type") == "http.response.start":
                status["code"] = message.get("status")
            await send(message)

        token = otel_context.attach(trace.set_span_in_context(span))
        try:
            await self._app(scope, receive, send_with_status)

            code = int(status["code"]) if status["code"] is not None else 200
            span.set_attribute("http.status_code", code)
            if code >= 500:
                span.set_status(Status(StatusCode.ERROR))
        except Exception as exception:
            span.set_status(Status(StatusCode.ERROR, str(exception)))
            span.record_exception(exception)
            raise
        finally:
            controller = GraftCodeAppInsightsRequestTelemetryMiddleware.resolve_controller_name(scope)
            if controller is not None:
                span.update_name(GraftCodeAppInsightsRequestTelemetryMiddleware.resolve_operation_name(
                    self._telemetry.service_name, method, path, controller))

            otel_context.detach(token)
            span.end()

    @staticmethod
    def resolve_operation_name(service_name: str, method: str, path: str | None, controller: str | None) -> str:
        if controller is not None and controller.strip():
            return service_name + "." + controller

        return service_name + " " + method + " " + (path or "/")

    @staticmethod
    def resolve_controller_name(scope: Scope) -> str | None:
        state = scope.get("state")
        explicit = state.get(GraftCodeAppInsightsRequestTelemetryMiddleware.CONTROLLER_ATTRIBUTE) \
            if isinstance(state, dict) else None
        if isinstance(explicit, str) and explicit.strip():
            return explicit

        owner = GraftCodeAppInsightsRequestTelemetryMiddleware._owner_name(scope.get("endpoint"))
        if owner is None:
            return None

        if owner.endswith(_CONTROLLER_SUFFIX) and len(owner) > len(_CONTROLLER_SUFFIX):
            return owner[:-len(_CONTROLLER_SUFFIX)]
        return owner

    @staticmethod
    def _owner_name(endpoint: object) -> str | None:
        if endpoint is None:
            return None

        bound_to = getattr(endpoint, "__self__", None)
        if bound_to is not None:
            return (bound_to if isinstance(bound_to, type) else type(bound_to)).__name__

        qualified = getattr(endpoint, "__qualname__", "")
        parts = [part for part in qualified.split(".") if part != "<locals>"]
        return parts[-2] if len(parts) >= 2 else None

    @staticmethod
    def _resolve_path(scope: Scope) -> str:
        path = str(scope.get("path") or "/")
        root_path = str(scope.get("root_path") or "")
        if root_path and path.startswith(root_path):
            path = path[len(root_path):]
        return path or "/"
