from __future__ import annotations

import os
import sys
import threading
import time
import traceback
from typing import IO, Any, Optional, TextIO, Union

from opentelemetry import context as otel_context
from opentelemetry._logs import Logger as OTelLogger, LogRecord
from opentelemetry._logs import SeverityNumber

from hypertube.utils.logging.LogsLevel import LogsLevel

from graftcode_telemetry_appinsights.MessageTemplate import MessageTemplate

_CONSOLE_INDENT = "      "

_SEVERITIES = {
    LogsLevel.Trace: SeverityNumber.TRACE,
    LogsLevel.Debug: SeverityNumber.DEBUG,
    LogsLevel.Info: SeverityNumber.INFO,
    LogsLevel.Warning: SeverityNumber.WARN,
    LogsLevel.Error: SeverityNumber.ERROR,
}

_CONSOLE_LEVELS = {
    LogsLevel.Trace: "trce",
    LogsLevel.Debug: "dbug",
    LogsLevel.Info: "info",
    LogsLevel.Warning: "warn",
}


class GraftCodeAppInsightsLogger:

    def __init__(
            self,
            category: str,
            logger: OTelLogger,
            minimum_level: LogsLevel,
            console: Union[bool, TextIO, None],
    ) -> None:
        self._category = category
        self._logger = logger
        self._minimum_level = minimum_level
        self._console = console
        self._console_lock = threading.Lock()

    def is_enabled(self, level: LogsLevel) -> bool:
        return (level != LogsLevel.Off
                and self._minimum_level != LogsLevel.Off
                and level.is_at_least(self._minimum_level))

    def trace(self, template: str, *args: Any) -> None:
        self.log(LogsLevel.Trace, template, *args)

    def debug(self, template: str, *args: Any) -> None:
        self.log(LogsLevel.Debug, template, *args)

    def info(self, template: str, *args: Any) -> None:
        self.log(LogsLevel.Info, template, *args)

    def warning(self, template: str, *args: Any) -> None:
        self.log(LogsLevel.Warning, template, *args)

    def error(self, exception_or_template: Union[BaseException, str], *args: Any) -> None:
        if isinstance(exception_or_template, BaseException):
            self.log_exception(LogsLevel.Error, exception_or_template, args[0] if args else None, *args[1:])
        else:
            self.log(LogsLevel.Error, exception_or_template, *args)

    def log(self, level: LogsLevel, template: Optional[str], *args: Any) -> None:
        self.log_exception(level, None, template, *args)

    def log_exception(
            self,
            level: LogsLevel,
            exception: Optional[BaseException],
            template: Optional[str],
            *args: Any,
    ) -> None:
        if not self.is_enabled(level):
            return

        properties: dict[str, str] = {}
        message = MessageTemplate.render(template, args, properties)

        self._emit(level, exception, template, message, properties)
        self._write_to_console(level, exception, message)

    def _emit(
            self,
            level: LogsLevel,
            exception: Optional[BaseException],
            template: Optional[str],
            message: str,
            properties: dict[str, str],
    ) -> None:
        attributes = dict(properties)

        if template is not None:
            attributes["{OriginalFormat}"] = template

        if exception is not None:
            exception_type = type(exception)
            attributes["exception.type"] = (
                exception_type.__module__ + "." + exception_type.__qualname__
                if exception_type.__module__ != "builtins"
                else exception_type.__qualname__)
            if str(exception):
                attributes["exception.message"] = str(exception)
            attributes["exception.stacktrace"] = GraftCodeAppInsightsLogger._stack_trace(exception)

        self._logger.emit(LogRecord(
            timestamp=time.time_ns(),
            context=otel_context.get_current(),
            severity_number=_SEVERITIES[level],
            severity_text=GraftCodeAppInsightsLogger._to_level_name(level),
            body=message,
            attributes=attributes))

    def _write_to_console(
            self,
            level: LogsLevel,
            exception: Optional[BaseException],
            message: str,
    ) -> None:
        console: Optional[IO[str]] = self._console
        if console is None:
            return

        if console is True:
            console = sys.stdout

        line = (_CONSOLE_LEVELS.get(level, "fail") + ": " + self._category + "[0]" + os.linesep
                + _CONSOLE_INDENT + message)
        if exception is not None:
            line += os.linesep + GraftCodeAppInsightsLogger._stack_trace(exception).strip()

        with self._console_lock:
            console.write(line + os.linesep)
            console.flush()

    @staticmethod
    def _stack_trace(exception: BaseException) -> str:
        return "".join(traceback.format_exception(type(exception), exception, exception.__traceback__))

    @staticmethod
    def _to_level_name(level: LogsLevel) -> str:
        return "Information" if level == LogsLevel.Info else level.name
