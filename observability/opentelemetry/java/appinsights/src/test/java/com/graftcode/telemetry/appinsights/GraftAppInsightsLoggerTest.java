package com.graftcode.telemetry.appinsights;

import com.hypertube.utils.logging.HypertubeLogger;
import com.hypertube.utils.logging.LogsLevel;
import io.opentelemetry.api.common.AttributeKey;
import io.opentelemetry.api.logs.Severity;
import io.opentelemetry.api.trace.Span;
import io.opentelemetry.context.Scope;
import io.opentelemetry.sdk.logs.data.LogRecordData;
import io.opentelemetry.sdk.testing.exporter.InMemoryLogRecordExporter;
import io.opentelemetry.sdk.logs.SdkLoggerProvider;
import io.opentelemetry.sdk.logs.export.SimpleLogRecordProcessor;
import org.junit.jupiter.api.Assertions;
import org.junit.jupiter.api.Test;

import java.io.ByteArrayOutputStream;
import java.io.PrintStream;
import java.nio.charset.StandardCharsets;
import java.util.HashMap;
import java.util.Map;

class GraftAppInsightsLoggerTest {

    @Test
    void template_IsRendered_AndPlaceholdersBecomeAttributes() {
        try (InMemoryTelemetry telemetry = new InMemoryTelemetry(LogsLevel.Info)) {
            Span span = telemetry.pipeline.startSpan("request");
            try (Scope ignored = span.makeCurrent()) {
                telemetry.pipeline.getLogger().info("Caller before Graft call Input={Input} TraceId={TraceId}", "Pawel6", null);
            } finally {
                span.end();
            }

            LogRecordData entry = single(telemetry);
            Assertions.assertEquals("Caller before Graft call Input=Pawel6 TraceId=(null)", entry.getBodyValue().asString());
            Assertions.assertEquals(Severity.INFO, entry.getSeverity());
            Assertions.assertEquals("Information", entry.getSeverityText());
            Assertions.assertEquals("Pawel6", entry.getAttributes().get(AttributeKey.stringKey("Input")));
            Assertions.assertEquals(span.getSpanContext().getSpanId(), entry.getSpanContext().getSpanId());
        }
    }

    @Test
    void levelsBelowTheMinimum_AreDropped() {
        try (InMemoryTelemetry telemetry = new InMemoryTelemetry(LogsLevel.Warning)) {
            telemetry.pipeline.getLogger().info("ignored");
            telemetry.pipeline.getLogger().warning("kept");

            Assertions.assertEquals("kept", single(telemetry).getBodyValue().asString());
        }
    }

    @Test
    void exceptions_AreAttachedAsExceptionAttributes() {
        try (InMemoryTelemetry telemetry = new InMemoryTelemetry(LogsLevel.Info)) {
            telemetry.pipeline.getLogger().error(new IllegalStateException("boom"), "failed {Step}", "fetch");

            LogRecordData entry = single(telemetry);
            Assertions.assertEquals(Severity.ERROR, entry.getSeverity());
            Assertions.assertEquals(IllegalStateException.class.getName(), entry.getAttributes().get(AttributeKey.stringKey("exception.type")));
            Assertions.assertEquals("boom", entry.getAttributes().get(AttributeKey.stringKey("exception.message")));
        }
    }

    @Test
    void hypertubeLines_AreForwardedToTheSameLogPipeline() {
        LogsLevel previous = HypertubeLogger.getMinimumLevel();
        HypertubeLogger.setMinimumLevel(LogsLevel.Off);
        try (InMemoryTelemetry telemetry = new InMemoryTelemetry(LogsLevel.Info)) {
            HypertubeLogger.getLogger().log(LogsLevel.Info, "Hypertube invoking {CommandType}", "INVOKE_STATIC_METHOD");

            LogRecordData entry = single(telemetry);
            Assertions.assertEquals("Hypertube invoking INVOKE_STATIC_METHOD", entry.getBodyValue().asString());
            Assertions.assertEquals("Hypertube", entry.getInstrumentationScopeInfo().getName());
        } finally {
            HypertubeLogger.setMinimumLevel(previous);
        }
    }

    @Test
    void console_UsesTheDotNetConsoleLayout() {
        ByteArrayOutputStream buffer = new ByteArrayOutputStream();
        InMemoryLogRecordExporter exporter = InMemoryLogRecordExporter.create();
        SdkLoggerProvider provider = SdkLoggerProvider.builder().addLogRecordProcessor(SimpleLogRecordProcessor.create(exporter)).build();
        GraftAppInsightsLogger logger = new GraftAppInsightsLogger(
                "TestGgJavaWebApi",
                provider.get("TestGgJavaWebApi"),
                LogsLevel.Info,
                new PrintStream(buffer, true));

        logger.info("Caller after Graft call Result={Result}", "fetched");

        String output = new String(buffer.toByteArray(), StandardCharsets.UTF_8);
        Assertions.assertEquals(
                "info: TestGgJavaWebApi[0]" + System.lineSeparator() + "      Caller after Graft call Result=fetched" + System.lineSeparator(),
                output);
        provider.close();
    }

    @Test
    void messageTemplate_HandlesEscapesFormatsAndExtraBraces() {
        Map<String, String> properties = new HashMap<>();

        String rendered = MessageTemplate.render("{{literal}} {@Dto} {Count:D3} {Missing}", new Object[]{"dto", 7}, properties);

        Assertions.assertEquals("{literal} dto 7 {Missing}", rendered);
        Assertions.assertEquals("dto", properties.get("Dto"));
        Assertions.assertEquals("7", properties.get("Count"));
        Assertions.assertFalse(properties.containsKey("Missing"));
    }

    private static LogRecordData single(InMemoryTelemetry telemetry) {
        Assertions.assertEquals(1, telemetry.logs().size(), telemetry.logs().toString());
        return telemetry.logs().get(0);
    }
}
