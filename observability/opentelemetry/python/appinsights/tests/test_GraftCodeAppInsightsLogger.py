import io
import os

from opentelemetry import context as otel_context
from opentelemetry import trace
from opentelemetry._logs import SeverityNumber
from opentelemetry.sdk._logs import LoggerProvider
from opentelemetry.sdk._logs.export import InMemoryLogRecordExporter, SimpleLogRecordProcessor

from hypertube.utils.logging.HypertubeLogger import HypertubeLogger
from hypertube.utils.logging.LogsLevel import LogsLevel

from graftcode_telemetry_appinsights import GraftCodeAppInsightsLogger
from graftcode_telemetry_appinsights.MessageTemplate import MessageTemplate

from InMemoryTelemetry import InMemoryTelemetry


def single(telemetry):
    logs = telemetry.logs()
    assert len(logs) == 1, [log.body for log in logs]
    return logs[0]


def test_template_is_rendered_and_placeholders_become_attributes():
    with InMemoryTelemetry(LogsLevel.Info) as telemetry:
        span = telemetry.pipeline.start_span("request")
        token = otel_context.attach(trace.set_span_in_context(span))
        try:
            telemetry.pipeline.logger.info("Caller before Graft call Input={Input} TraceId={TraceId}", "Pawel6", None)
        finally:
            otel_context.detach(token)
            span.end()

        entry = single(telemetry)
        assert entry.body == "Caller before Graft call Input=Pawel6 TraceId=(null)"
        assert entry.severity_number == SeverityNumber.INFO
        assert entry.severity_text == "Information"
        assert entry.attributes["Input"] == "Pawel6"
        assert entry.attributes["{OriginalFormat}"] == "Caller before Graft call Input={Input} TraceId={TraceId}"
        assert entry.span_id == span.get_span_context().span_id


def test_levels_below_the_minimum_are_dropped():
    with InMemoryTelemetry(LogsLevel.Warning) as telemetry:
        telemetry.pipeline.logger.info("ignored")
        telemetry.pipeline.logger.warning("kept")

        assert single(telemetry).body == "kept"


def test_exceptions_are_attached_as_exception_attributes():
    with InMemoryTelemetry(LogsLevel.Info) as telemetry:
        telemetry.pipeline.logger.error(RuntimeError("boom"), "failed {Step}", "fetch")

        entry = single(telemetry)
        assert entry.severity_number == SeverityNumber.ERROR
        assert entry.body == "failed fetch"
        assert entry.attributes["exception.type"] == "RuntimeError"
        assert entry.attributes["exception.message"] == "boom"


def test_hypertube_lines_are_forwarded_to_the_same_log_pipeline():
    with InMemoryTelemetry(LogsLevel.Info) as telemetry:
        HypertubeLogger.get_logger().log(LogsLevel.Info, "Hypertube invoking {CommandType}", "InvokeStaticMethod")

        entry = single(telemetry)
        assert entry.body == "Hypertube invoking InvokeStaticMethod"
        assert entry.severity_text == "Information"
        assert entry.attributes["CommandType"] == "InvokeStaticMethod"
        assert telemetry.scopes()[0].name == "Hypertube"


def test_hypertube_exceptions_carry_exception_attributes_and_the_active_span():
    with InMemoryTelemetry(LogsLevel.Info) as telemetry:
        span = telemetry.pipeline.start_span("request")
        token = otel_context.attach(trace.set_span_in_context(span))
        try:
            HypertubeLogger.get_logger().log_exception(RuntimeError("boom"), "Receiver failed to process command")
        finally:
            otel_context.detach(token)
            span.end()

        entry = single(telemetry)
        assert entry.severity_number == SeverityNumber.ERROR
        assert entry.attributes["exception.type"] == "RuntimeError"
        assert entry.attributes["exception.message"] == "boom"
        assert entry.span_id == span.get_span_context().span_id


def test_hypertube_lines_below_the_minimum_are_not_forwarded():
    with InMemoryTelemetry(LogsLevel.Warning) as telemetry:
        HypertubeLogger.get_logger().log(LogsLevel.Info, "Hypertube invoking {CommandType}", "InvokeStaticMethod")

        assert telemetry.logs() == []


def test_console_uses_the_dotnet_console_layout():
    buffer = io.StringIO()
    exporter = InMemoryLogRecordExporter()
    provider = LoggerProvider()
    provider.add_log_record_processor(SimpleLogRecordProcessor(exporter))
    logger = GraftCodeAppInsightsLogger("TestGgPythonWebApi", provider.get_logger("TestGgPythonWebApi"), LogsLevel.Info,
                                        buffer)

    logger.info("Caller after Graft call Result={Result}", "fetched")

    assert buffer.getvalue() == (
        "info: TestGgPythonWebApi[0]" + os.linesep + "      Caller after Graft call Result=fetched" + os.linesep)
    provider.shutdown()


def test_message_template_handles_escapes_formats_and_extra_braces():
    properties = {}

    rendered = MessageTemplate.render("{{literal}} {@Dto} {Count:D3} {Missing}", ["dto", 7], properties)

    assert rendered == "{literal} dto 7 {Missing}"
    assert properties["Dto"] == "dto"
    assert properties["Count"] == "7"
    assert "Missing" not in properties
