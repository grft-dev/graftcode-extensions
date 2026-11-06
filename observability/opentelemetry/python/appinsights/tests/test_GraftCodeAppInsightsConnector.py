import pytest
from opentelemetry import context as otel_context
from opentelemetry import trace
from opentelemetry.sdk._logs import LoggerProvider
from opentelemetry.sdk._logs.export import BatchLogRecordProcessor, InMemoryLogRecordExporter
from opentelemetry.sdk.trace import TracerProvider
from opentelemetry.sdk.trace.export import BatchSpanProcessor
from opentelemetry.sdk.trace.export.in_memory_span_exporter import InMemorySpanExporter
from opentelemetry.trace import SpanKind

from graftcode_telemetry_appinsights import (
    GraftCodeAppInsightsConnector,
    GraftCodeAppInsightsConnectorOptions,
    GraftCodeAppInsightsConnectorPipeline,
)

LOCAL_CONNECTION_STRING = (
    "InstrumentationKey=00000000-0000-0000-0000-000000000001;"
    "IngestionEndpoint=http://127.0.0.1:1/;LiveEndpoint=http://127.0.0.1:1/")


def start_local():
    options = GraftCodeAppInsightsConnectorOptions(service_name="test.service", enable_console_logging=False)
    options._environment = lambda name: None
    return GraftCodeAppInsightsConnector.start(options)


def start_exporting(traces_per_second):
    def configure(options):
        options.service_name = "test.service"
        options.enable_console_logging = False
        options.connection_string = LOCAL_CONNECTION_STRING
        options.traces_per_second = traces_per_second

    return GraftCodeAppInsightsConnector.start(configure)


def test_start_requires_a_service_name():
    with pytest.raises(ValueError):
        GraftCodeAppInsightsConnector.start(GraftCodeAppInsightsConnectorOptions())


def test_start_rejects_a_non_positive_rate_limit():
    def configure(options):
        options.service_name = "test.service"
        options.traces_per_second = 0.0

    with pytest.raises(ValueError):
        GraftCodeAppInsightsConnector.start(configure)


def test_explicit_connection_string_wins_over_environment():
    environment = {
        GraftCodeAppInsightsConnectorOptions.CONNECTION_STRING_VARIABLE: "InstrumentationKey=11111111-1111-1111-1111-111111111111"}

    options = GraftCodeAppInsightsConnectorOptions(service_name="test")
    options.connection_string = "InstrumentationKey=22222222-2222-2222-2222-222222222222"
    options._environment = environment.get

    assert options.resolve_connection_string() == "InstrumentationKey=22222222-2222-2222-2222-222222222222"


def test_environment_connection_string_is_used_when_none_is_configured():
    options = GraftCodeAppInsightsConnectorOptions()

    options._environment = lambda name: "InstrumentationKey=1"
    assert options.resolve_connection_string() == "InstrumentationKey=1"

    options._environment = lambda name: "  "
    assert options.resolve_connection_string() is None


def test_start_works_without_a_connection_string():
    with start_local() as pipeline:
        span = pipeline.start_span("test.operation")
        token = otel_context.attach(trace.set_span_in_context(span))
        try:
            assert span.get_span_context().is_valid
            assert len(trace.format_trace_id(span.get_span_context().trace_id)) == 32
        finally:
            otel_context.detach(token)
            span.end()

        assert pipeline.force_flush()


def test_missing_connection_string_is_reported():
    with start_local() as pipeline:
        assert not pipeline.is_exporting_to_app_insights()


def test_every_trace_is_recorded_so_logs_are_not_sampled_away():
    with start_exporting(None) as pipeline:
        assert pipeline.is_exporting_to_app_insights()

        for i in range(25):
            span = pipeline.start_span("test.operation." + str(i), SpanKind.SERVER)
            assert span.is_recording(), "span " + str(i) + " was not recorded"
            span.end()


def test_trace_exporter_reads_the_resource_from_the_connector_tracer_provider():
    with start_exporting(None) as pipeline:
        processors = pipeline.tracer_provider._active_span_processor._span_processors
        exporters = [processor.span_exporter for processor in processors]

        assert any(getattr(exporter, "_tracer_provider", None) is pipeline.tracer_provider for exporter in exporters)


def test_rate_limited_sampling_can_be_opted_into():
    with start_exporting(0.0001) as pipeline:
        recorded = 0
        for i in range(25):
            span = pipeline.start_span("test.operation." + str(i), SpanKind.SERVER)
            if span.is_recording():
                recorded += 1
            span.end()

        assert recorded < 25, "the configured rate limit was ignored"


def test_force_flush_covers_logs_as_well_as_traces():
    spans = InMemorySpanExporter()
    logs = InMemoryLogRecordExporter()
    tracer_provider = TracerProvider()
    tracer_provider.add_span_processor(BatchSpanProcessor(spans, schedule_delay_millis=3_600_000))
    logger_provider = LoggerProvider()
    logger_provider.add_log_record_processor(BatchLogRecordProcessor(logs, schedule_delay_millis=3_600_000))
    options = GraftCodeAppInsightsConnectorOptions(service_name="test.service", enable_console_logging=False)

    with GraftCodeAppInsightsConnectorPipeline(options, (tracer_provider, logger_provider)) as pipeline:
        span = pipeline.start_span("test.operation")
        token = otel_context.attach(trace.set_span_in_context(span))
        try:
            pipeline.logger.info("Test log {Value}", 42)
        finally:
            otel_context.detach(token)
            span.end()

        assert spans.get_finished_spans() == ()
        assert logs.get_finished_logs() == ()

        assert pipeline.force_flush(10000)

        assert len(spans.get_finished_spans()) == 1
        assert len(logs.get_finished_logs()) == 1


def test_close_is_idempotent_and_flush_after_close_succeeds():
    pipeline = start_local()
    pipeline.close()
    pipeline.close()

    assert pipeline.force_flush()
