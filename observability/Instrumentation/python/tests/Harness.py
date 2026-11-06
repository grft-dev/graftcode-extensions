from __future__ import annotations

import logging
import uuid
from typing import Any, Mapping, Optional

from opentelemetry import trace
from opentelemetry.sdk.trace import TracerProvider
from opentelemetry.sdk.trace.export import SimpleSpanProcessor
from opentelemetry.sdk.trace.export.in_memory_span_exporter import InMemorySpanExporter
from opentelemetry.sdk.trace.sampling import ALWAYS_OFF, ALWAYS_ON

from hypertube.utils.Command import Command
from hypertube.utils.CommandType import CommandType
from hypertube.utils.RuntimeName import RuntimeName
from hypertube.utils.abstractions.HypertubeCallContext import HypertubeCallContext
from hypertube.utils.connectionData.InMemoryConnectionData import InMemoryConnectionData
from hypertube.utils.logging.LogsLevel import LogsLevel

from graftcode_instrumentation import GraftcodeInstrumentation, GraftcodeTelemetrySettings
from graftcode_instrumentation.CallSpan import CallSpan

NO_HEADERS: Mapping[str, str] = {}

_PYTHON_LEVELS = {
    LogsLevel.Trace: 5,
    LogsLevel.Debug: logging.DEBUG,
    LogsLevel.Info: logging.INFO,
    LogsLevel.Warning: logging.WARNING,
    LogsLevel.Error: logging.ERROR,
    LogsLevel.Off: logging.CRITICAL + 1,
}


class CapturedLog:
    def __init__(self, record: logging.LogRecord, span_id: Any) -> None:
        self.record = record
        self.level = record.levelno
        self.body = record.getMessage()
        self.span_id = span_id


class _CapturingHandler(logging.Handler):
    def __init__(self, logs: list[CapturedLog]) -> None:
        super().__init__(level=0)
        self._logs = logs

    def emit(self, record: logging.LogRecord) -> None:
        self._logs.append(CapturedLog(record, trace.get_current_span().get_span_context().span_id))


class Harness:
    def __init__(
            self,
            recording: bool = True,
            minimum_log_level: Optional[LogsLevel] = None,
    ) -> None:
        self._auto_inject = GraftcodeTelemetrySettings.is_auto_inject_enabled()
        self._capture_bodies = GraftcodeTelemetrySettings.is_body_capture_enabled()
        self._body_max_characters = GraftcodeTelemetrySettings.get_body_max_characters()
        self._spans = InMemorySpanExporter()
        self._logs: list[CapturedLog] = []
        self.provider = TracerProvider(sampler=ALWAYS_ON if recording else ALWAYS_OFF)
        self.provider.add_span_processor(SimpleSpanProcessor(self._spans))

        logger: Optional[logging.Logger] = None
        if minimum_log_level is not None:
            logger = logging.getLogger("Hypertube.tests." + uuid.uuid4().hex)
            logger.propagate = False
            logger.setLevel(_PYTHON_LEVELS[minimum_log_level])
            logger.addHandler(_CapturingHandler(self._logs))

        self._logger = logger

        self.listener = GraftcodeInstrumentation.attach(self.provider, logger)

    def on_invoke(
            self,
            command: Command,
            connection_data: InMemoryConnectionData,
    ) -> Optional[CallSpan]:
        context = HypertubeCallContext()
        self.listener.on_execute(context, command, connection_data)
        return context.items[0] if context.items else None

    def on_handle(
            self,
            command: Command,
            headers: Mapping[str, str],
    ) -> Optional[CallSpan]:
        context = HypertubeCallContext()
        handled = command
        if headers:
            pairs = []
            for key, value in headers.items():
                pairs.append(key)
                pairs.append(value)

            handled = Command(
                command.runtime_name,
                CommandType.ContextMetadata,
                [Command(command.runtime_name, CommandType.Array, pairs), command])

        self.listener.on_process(context, handled)
        return context.items[0] if context.items else None

    @staticmethod
    def complete(
            span: Optional[CallSpan],
            response: Optional[Command],
            exception: Optional[BaseException],
    ) -> None:
        if span is not None:
            span.complete(response, exception)

    def stopped(self) -> list[Any]:
        return list(self._spans.get_finished_spans())

    def logs(self) -> list[CapturedLog]:
        return self._logs

    def close(self) -> None:
        self.listener.close()
        self.provider.shutdown()
        if self._logger is not None:
            self._logger.handlers.clear()
        GraftcodeTelemetrySettings.set_auto_inject_enabled(self._auto_inject)
        GraftcodeTelemetrySettings.set_body_capture_enabled(self._capture_bodies)
        GraftcodeTelemetrySettings._body_max_characters = self._body_max_characters
        InMemoryConnectionData().set_thread_local_headers({})

    def __enter__(self) -> Harness:
        return self

    def __exit__(
            self,
            exc_type: Optional[type[BaseException]],
            exc: Optional[BaseException],
            traceback: Any,
    ) -> bool:
        self.close()
        return False


class Commands:
    @staticmethod
    def of(command_type: CommandType, *payload: Any) -> Command:
        return Command(RuntimeName.python, command_type, list(payload))

    @staticmethod
    def value(value: Any) -> Command:
        return Command(RuntimeName.python, CommandType.Value, [value])

    @staticmethod
    def fetch() -> Command:
        return Commands.of(
            CommandType.InvokeStaticMethod,
            Commands.of(CommandType.GetType, Commands.value("SomeLib.Service")),
            Commands.value("Fetch"))
