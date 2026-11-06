from __future__ import annotations

import logging
import traceback
from types import TracebackType
from typing import Any, Dict, Mapping, Optional, Type

from opentelemetry import trace
from opentelemetry.sdk.trace import TracerProvider
from opentelemetry.trace import Tracer

from hypertube.utils.abstractions.HypertubeBroker import HypertubeBroker
from hypertube.utils.abstractions.HypertubeCallContext import HypertubeCallContext
from hypertube.utils.abstractions.IHypertubeHandling import IHypertubeHandling
from hypertube.utils.abstractions.IHypertubeInvocation import IHypertubeInvocation
from hypertube.utils.abstractions.IHypertubeLogging import IHypertubeLogging
from hypertube.utils.Command import Command
from hypertube.utils.connectionData.IConnectionData import IConnectionData
from hypertube.utils.logging.HypertubeLogger import HypertubeLogger
from hypertube.utils.logging.LogsCommandResolver import LogsCommandResolver
from hypertube.utils.logging.LogsLevel import LogsLevel
from hypertube.utils.logging.LogsRedactor import LogsRedactor

from graftcode_instrumentation.CallSpan import CallSpan
from graftcode_instrumentation.OperationName import OperationName

TRACE = 5

_LOG_LEVELS: Dict[LogsLevel, int] = {
    LogsLevel.Trace: TRACE,
    LogsLevel.Debug: logging.DEBUG,
    LogsLevel.Info: logging.INFO,
    LogsLevel.Warning: logging.WARNING,
    LogsLevel.Error: logging.ERROR,
}

_ENABLED_PROBE_LEVELS = (logging.CRITICAL, logging.ERROR, logging.WARNING, logging.INFO, logging.DEBUG, TRACE)

_RESERVED_RECORD_ATTRIBUTES = frozenset(
    logging.LogRecord("", 0, "", 0, "", None, None).__dict__.keys()) | {"message", "asctime"}


class GraftcodeInstrumentation(IHypertubeInvocation, IHypertubeHandling, IHypertubeLogging):
    INSTRUMENTATION_NAME = "Graftcode"
    LOGGER_NAME = "Hypertube"

    def __init__(
            self,
            tracer_provider: Optional[TracerProvider] = None,
            logger: Optional[logging.Logger] = None,
    ) -> None:
        self._tracer: Tracer = trace.get_tracer(
            GraftcodeInstrumentation.INSTRUMENTATION_NAME,
            tracer_provider=tracer_provider,
        )
        self._logger = logger
        HypertubeBroker.attach(self)
        HypertubeLogger.attach(self)

    @staticmethod
    def attach(
            tracer_provider: Optional[TracerProvider] = None,
            logger: Optional[logging.Logger] = None,
    ) -> GraftcodeInstrumentation:
        return GraftcodeInstrumentation(tracer_provider, logger)

    def on_execute(
            self,
            context: HypertubeCallContext,
            command: Command,
            connection_data: IConnectionData,
    ) -> None:
        reported = LogsCommandResolver.resolve_for_invoke(command)
        span = CallSpan.start_invoke(self._tracer, reported, connection_data)
        if span is not None:
            context.items.append(span)

    def on_response(self, context: HypertubeCallContext, response: Any) -> None:
        GraftcodeInstrumentation._complete_spans(context, response, None)

    def on_invocation_error(self, context: HypertubeCallContext, exception: BaseException) -> None:
        GraftcodeInstrumentation._complete_spans(context, None, exception)

    def on_process(self, context: HypertubeCallContext, command: Command) -> None:
        headers: Dict[str, str] = {}
        reported = LogsCommandResolver.resolve_for_handle(command, headers)
        span = CallSpan.start_handle(self._tracer, reported, headers)
        if span is not None:
            context.items.append(span)

    def on_processed(self, context: HypertubeCallContext, response: Any) -> None:
        GraftcodeInstrumentation._complete_spans(context, response, None)

    def on_handling_error(self, context: HypertubeCallContext, exception: BaseException) -> None:
        GraftcodeInstrumentation._complete_spans(context, None, exception)

    def is_requested(self) -> bool:
        if self._logger is None:
            return trace.get_current_span().is_recording()

        return any(self._logger.isEnabledFor(level) for level in _ENABLED_PROBE_LEVELS)

    def is_log_enabled(self, level: LogsLevel) -> bool:
        if self._logger is None:
            return trace.get_current_span().is_recording()

        python_level = _LOG_LEVELS.get(level)
        return python_level is not None and self._logger.isEnabledFor(python_level)

    def on_log(
            self,
            level: LogsLevel,
            message: str,
            properties: Mapping[str, Any],
            exception: Optional[BaseException],
    ) -> None:
        if self._logger is None:
            GraftcodeInstrumentation._record_span_event(level, message, properties, exception)
            return

        if not self.is_log_enabled(level):
            return

        extra: Dict[str, Any] = {}
        for key, value in properties.items():
            extra[key + "_" if key in _RESERVED_RECORD_ATTRIBUTES else key] = value

        exc_info = (type(exception), exception, exception.__traceback__) if exception is not None else None
        self._logger.log(_LOG_LEVELS[level], message, exc_info=exc_info, extra=extra)

    def close(self) -> None:
        HypertubeBroker.detach(self)
        HypertubeLogger.detach(self)

    def dispose(self) -> None:
        self.close()

    def __enter__(self) -> GraftcodeInstrumentation:
        return self

    def __exit__(
            self,
            exc_type: Optional[Type[BaseException]],
            exc: Optional[BaseException],
            exc_traceback: Optional[TracebackType],
    ) -> bool:
        self.close()
        return False

    @staticmethod
    def _complete_spans(context: HypertubeCallContext, response: Any, exception: Optional[BaseException]) -> None:
        items = context.items
        for i in range(len(items) - 1, -1, -1):
            item = items[i]
            if isinstance(item, CallSpan):
                item.complete(response, exception)
                del items[i]

    @staticmethod
    def _record_span_event(
            level: LogsLevel,
            message: str,
            properties: Mapping[str, Any],
            exception: Optional[BaseException],
    ) -> None:
        span = trace.get_current_span()
        if not span.is_recording():
            return

        attributes: Dict[str, Any] = {"log.level": GraftcodeInstrumentation._to_level_name(level)}
        attributes.update(properties)

        if exception is not None:
            attributes["log.message"] = message
            attributes["exception.type"] = OperationName._full_name(type(exception))
            GraftcodeInstrumentation._put_if_present(
                attributes, "exception.message", LogsRedactor.redact_value(str(exception)))
            GraftcodeInstrumentation._put_if_present(
                attributes,
                "exception.stacktrace",
                LogsRedactor.redact_value(GraftcodeInstrumentation._stack_trace(exception)),
            )

        span.add_event("exception" if exception is not None else message, attributes)

    @staticmethod
    def _stack_trace(exception: BaseException) -> str:
        return "".join(traceback.format_exception(type(exception), exception, exception.__traceback__))

    @staticmethod
    def _put_if_present(attributes: Dict[str, Any], key: str, value: Any) -> None:
        if value:
            attributes[key] = value

    @staticmethod
    def _to_level_name(level: LogsLevel) -> str:
        return "Information" if level == LogsLevel.Info else level.name
