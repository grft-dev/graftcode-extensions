from opentelemetry.sdk._logs import LoggerProvider
from opentelemetry.sdk._logs.export import InMemoryLogRecordExporter, SimpleLogRecordProcessor
from opentelemetry.sdk.trace import TracerProvider
from opentelemetry.sdk.trace.export import SimpleSpanProcessor
from opentelemetry.sdk.trace.export.in_memory_span_exporter import InMemorySpanExporter

from graftcode_telemetry_appinsights import GraftCodeAppInsightsConnectorOptions, GraftCodeAppInsightsConnectorPipeline


class InMemoryTelemetry:

    def __init__(self, minimum_log_level):
        self._spans = InMemorySpanExporter()
        self._logs = InMemoryLogRecordExporter()
        tracer_provider = TracerProvider()
        tracer_provider.add_span_processor(SimpleSpanProcessor(self._spans))
        logger_provider = LoggerProvider()
        logger_provider.add_log_record_processor(SimpleLogRecordProcessor(self._logs))

        options = GraftCodeAppInsightsConnectorOptions(
            service_name="TestService",
            minimum_log_level=minimum_log_level,
            enable_console_logging=False)
        self.pipeline = GraftCodeAppInsightsConnectorPipeline(options, (tracer_provider, logger_provider))

    def spans(self):
        return list(self._spans.get_finished_spans())

    def logs(self):
        return [item.log_record for item in self._logs.get_finished_logs()]

    def scopes(self):
        return [item.instrumentation_scope for item in self._logs.get_finished_logs()]

    def close(self):
        self.pipeline.close()

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, traceback):
        self.close()
        return False
