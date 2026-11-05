import atexit
import logging
import sys
import threading
import traceback

from opentelemetry import context as otel_context
from opentelemetry._logs import LogRecord, SeverityNumber
from opentelemetry.sdk._logs import LoggerProvider
from opentelemetry.sdk._logs.export import BatchLogRecordProcessor
from opentelemetry.sdk.resources import Resource
from opentelemetry.sdk.trace import TracerProvider
from opentelemetry.sdk.trace.export import BatchSpanProcessor
from opentelemetry.sdk.trace.sampling import ALWAYS_ON
from opentelemetry.trace import SpanKind

from hypertube.utils.logging.LogsLevel import LogsLevel

from graftcode_instrumentation import GraftcodeInstrumentation

from graftcode_telemetry_appinsights.GraftCodeAppInsightsConnectorOptions import GraftCodeAppInsightsConnectorOptions
from graftcode_telemetry_appinsights.GraftCodeAppInsightsLogger import GraftCodeAppInsightsLogger
from graftcode_telemetry_appinsights.RateLimitedSampler import RateLimitedSampler

_DEFAULT_FLUSH_MILLISECONDS = 5000

_MISSING_CONNECTION_STRING = (
    "Application Insights telemetry for {ServiceName} is disabled: no connection string was configured. "
    "Set " + GraftCodeAppInsightsConnectorOptions.CONNECTION_STRING_VARIABLE + " in this process or set "
    "GraftCodeAppInsightsConnectorOptions.connection_string.")

_PYTHON_LEVELS = {
    LogsLevel.Trace: 5,
    LogsLevel.Debug: logging.DEBUG,
    LogsLevel.Info: logging.INFO,
    LogsLevel.Warning: logging.WARNING,
    LogsLevel.Error: logging.ERROR,
    LogsLevel.Off: logging.CRITICAL + 1,
}

_STANDARD_RECORD_ATTRIBUTES = frozenset(
    logging.LogRecord("", 0, "", 0, "", None, None).__dict__.keys()) | {"message", "asctime", "taskName"}


class _HypertubeLogHandler(logging.Handler):

    def __init__(self, logger_provider):
        super().__init__(level=0)
        self._logger = logger_provider.get_logger(GraftcodeInstrumentation.LOGGER_NAME)

    def emit(self, record):
        try:
            attributes = {
                key: value for key, value in record.__dict__.items()
                if key not in _STANDARD_RECORD_ATTRIBUTES and isinstance(value, (str, bool, int, float))
            }

            if record.exc_info and record.exc_info[1] is not None:
                exception = record.exc_info[1]
                attributes["exception.type"] = type(exception).__qualname__
                if str(exception):
                    attributes["exception.message"] = str(exception)
                attributes["exception.stacktrace"] = "".join(traceback.format_exception(*record.exc_info))

            self._logger.emit(LogRecord(
                timestamp=int(record.created * 1e9),
                context=otel_context.get_current(),
                severity_number=_HypertubeLogHandler._severity(record.levelno),
                severity_text="Information" if record.levelno == logging.INFO else record.levelname.capitalize(),
                body=record.getMessage(),
                attributes=attributes))
        except Exception:
            self.handleError(record)

    @staticmethod
    def _severity(level):
        if level >= logging.ERROR:
            return SeverityNumber.ERROR
        if level >= logging.WARNING:
            return SeverityNumber.WARN
        if level >= logging.INFO:
            return SeverityNumber.INFO
        if level >= logging.DEBUG:
            return SeverityNumber.DEBUG
        return SeverityNumber.TRACE


class GraftCodeAppInsightsConnectorPipeline:

    def __init__(self, options, providers=None):
        options.validate()

        self._flush_gate = threading.Lock()
        self._closed = False
        self.service_name = options.service_name
        connection_string = options.resolve_connection_string() if providers is None else None
        self._exporting_to_app_insights = connection_string is not None

        if providers is not None:
            self.tracer_provider, self.logger_provider = providers
        elif connection_string is not None:
            self.tracer_provider, self.logger_provider = GraftCodeAppInsightsConnectorPipeline._build_exporting_sdk(
                self.service_name, connection_string, options.traces_per_second)
        else:
            self.tracer_provider, self.logger_provider = GraftCodeAppInsightsConnectorPipeline._build_local_sdk(
                self.service_name)

        self.tracer = self.tracer_provider.get_tracer(self.service_name)
        self.logger = GraftCodeAppInsightsLogger(
            self.service_name,
            self.logger_provider.get_logger(self.service_name),
            options.minimum_log_level,
            True if options.enable_console_logging else None)

        self._hypertube_logger = logging.getLogger(GraftcodeInstrumentation.LOGGER_NAME)
        self._hypertube_logger.setLevel(_PYTHON_LEVELS[options.minimum_log_level])
        self._hypertube_logger.propagate = False
        self._log_handler = _HypertubeLogHandler(self.logger_provider)
        self._hypertube_logger.addHandler(self._log_handler)
        self.instrumentation = GraftcodeInstrumentation.attach(self.tracer_provider, self._hypertube_logger)

        if providers is None and connection_string is None:
            self._warn_about_missing_connection_string(options)

        atexit.register(self.close)

        self._previous_excepthook = sys.excepthook
        self._previous_thread_excepthook = threading.excepthook
        sys.excepthook = self._on_uncaught_exception
        threading.excepthook = self._on_uncaught_thread_exception

    def is_exporting_to_app_insights(self):
        return self._exporting_to_app_insights

    def start_span(self, name, kind=SpanKind.INTERNAL, context=None):
        return self.tracer.start_span(name, context=context, kind=kind)

    def force_flush(self, timeout_millis=_DEFAULT_FLUSH_MILLISECONDS):
        with self._flush_gate:
            if self._closed:
                return True

            traces = self.tracer_provider.force_flush(timeout_millis)
            logs = self.logger_provider.force_flush(timeout_millis)
            return bool(traces) and bool(logs)

    def close(self):
        with self._flush_gate:
            if self._closed:
                return

            self.tracer_provider.force_flush(_DEFAULT_FLUSH_MILLISECONDS)
            self.logger_provider.force_flush(_DEFAULT_FLUSH_MILLISECONDS)
            self._closed = True

        self.instrumentation.close()
        self._hypertube_logger.removeHandler(self._log_handler)

        try:
            atexit.unregister(self.close)
        except Exception:
            pass

        if sys.excepthook == self._on_uncaught_exception:
            sys.excepthook = self._previous_excepthook
        if threading.excepthook == self._on_uncaught_thread_exception:
            threading.excepthook = self._previous_thread_excepthook

        self.tracer_provider.shutdown()
        self.logger_provider.shutdown()

    def dispose(self):
        self.close()

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, traceback):
        self.close()
        return False

    @staticmethod
    def _build_exporting_sdk(service_name, connection_string, traces_per_second):
        from azure.monitor.opentelemetry.exporter import AzureMonitorLogExporter, AzureMonitorTraceExporter

        sampler = ALWAYS_ON if traces_per_second is None else RateLimitedSampler(traces_per_second)
        resource = Resource.create({"service.name": service_name})

        tracer_provider = TracerProvider(resource=resource, sampler=sampler)
        tracer_provider.add_span_processor(BatchSpanProcessor(AzureMonitorTraceExporter(
            connection_string=connection_string, tracer_provider=tracer_provider)))

        logger_provider = LoggerProvider(resource=resource)
        logger_provider.add_log_record_processor(
            BatchLogRecordProcessor(AzureMonitorLogExporter(connection_string=connection_string)))

        return tracer_provider, logger_provider

    @staticmethod
    def _build_local_sdk(service_name):
        resource = Resource.create({"service.name": service_name})
        return TracerProvider(resource=resource), LoggerProvider(resource=resource)

    def _warn_about_missing_connection_string(self, options):
        self.logger.log(LogsLevel.Warning, _MISSING_CONNECTION_STRING, self.service_name)

        if not options.enable_console_logging:
            sys.stderr.write(_MISSING_CONNECTION_STRING.replace("{ServiceName}", self.service_name) + "\n")

    def _on_uncaught_exception(self, exc_type, exc_value, exc_traceback):
        self.force_flush()
        self._previous_excepthook(exc_type, exc_value, exc_traceback)

    def _on_uncaught_thread_exception(self, args):
        self.force_flush()
        self._previous_thread_excepthook(args)
