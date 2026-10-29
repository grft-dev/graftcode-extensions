package com.graftcode.telemetry.appinsights;

import io.opentelemetry.api.trace.Span;
import io.opentelemetry.api.trace.SpanKind;
import io.opentelemetry.context.Scope;
import io.opentelemetry.sdk.OpenTelemetrySdk;
import io.opentelemetry.sdk.logs.SdkLoggerProvider;
import io.opentelemetry.sdk.logs.export.BatchLogRecordProcessor;
import io.opentelemetry.sdk.testing.exporter.InMemoryLogRecordExporter;
import io.opentelemetry.sdk.testing.exporter.InMemorySpanExporter;
import io.opentelemetry.sdk.trace.SdkTracerProvider;
import io.opentelemetry.sdk.trace.export.BatchSpanProcessor;
import org.junit.jupiter.api.Assertions;
import org.junit.jupiter.api.Test;

import java.time.Duration;
import java.util.HashMap;
import java.util.Map;

class GraftCodeAppInsightsConnectorTest {
    private static final String LOCAL_CONNECTION_STRING =
            "InstrumentationKey=00000000-0000-0000-0000-000000000001;"
                    + "IngestionEndpoint=http://127.0.0.1:1/;LiveEndpoint=http://127.0.0.1:1/";

    @Test
    void start_RequiresAServiceName() {
        Assertions.assertThrows(
                IllegalStateException.class,
                () -> GraftCodeAppInsightsConnector.start(new GraftCodeAppInsightsConnectorOptions()));
    }

    @Test
    void start_RejectsANonPositiveRateLimit() {
        Assertions.assertThrows(IllegalStateException.class, () -> GraftCodeAppInsightsConnector.start(options -> {
            options.setServiceName("test.service");
            options.setTracesPerSecond(0.0);
        }));
    }

    @Test
    void explicitConnectionString_WinsOverEnvironment() {
        Map<String, String> environment = new HashMap<>();
        environment.put(GraftCodeAppInsightsConnectorOptions.CONNECTION_STRING_VARIABLE, "InstrumentationKey=11111111-1111-1111-1111-111111111111");

        GraftCodeAppInsightsConnectorOptions options = new GraftCodeAppInsightsConnectorOptions();
        options.setServiceName("test");
        options.setConnectionString("InstrumentationKey=22222222-2222-2222-2222-222222222222");
        options.setEnvironment(environment::get);

        Assertions.assertEquals("InstrumentationKey=22222222-2222-2222-2222-222222222222", options.resolveConnectionString());
    }

    @Test
    void environmentConnectionString_IsUsedWhenNoneIsConfigured() {
        GraftCodeAppInsightsConnectorOptions options = new GraftCodeAppInsightsConnectorOptions();

        options.setEnvironment(name -> "InstrumentationKey=1");
        Assertions.assertEquals("InstrumentationKey=1", options.resolveConnectionString());

        options.setEnvironment(name -> "  ");
        Assertions.assertNull(options.resolveConnectionString());
    }

    @Test
    void start_WorksWithoutAConnectionString() {
        try (GraftCodeAppInsightsConnectorPipeline pipeline = startLocal()) {
            Span span = pipeline.startSpan("test.operation");
            try (Scope ignored = span.makeCurrent()) {
                Assertions.assertTrue(span.getSpanContext().isValid());
                Assertions.assertEquals(32, span.getSpanContext().getTraceId().length());
            } finally {
                span.end();
            }

            Assertions.assertTrue(pipeline.forceFlush());
        }
    }

    @Test
    void missingConnectionString_IsReported() {
        try (GraftCodeAppInsightsConnectorPipeline pipeline = startLocal()) {
            Assertions.assertFalse(pipeline.isExportingToAppInsights());
        }
    }

    @Test
    void everyTraceIsRecorded_SoLogsAreNotSampledAway() {
        try (GraftCodeAppInsightsConnectorPipeline pipeline = startExporting(null)) {
            Assertions.assertTrue(pipeline.isExportingToAppInsights());

            for (int i = 0; i < 25; i++) {
                Span span = pipeline.startSpan("test.operation." + i, SpanKind.SERVER);
                Assertions.assertTrue(span.isRecording(), "span " + i + " was not recorded");
                span.end();
            }
        }
    }

    @Test
    void rateLimitedSampling_CanBeOptedInto() {
        try (GraftCodeAppInsightsConnectorPipeline pipeline = startExporting(0.0001)) {
            int recorded = 0;
            for (int i = 0; i < 25; i++) {
                Span span = pipeline.startSpan("test.operation." + i, SpanKind.SERVER);
                if (span.isRecording()) {
                    recorded++;
                }
                span.end();
            }

            Assertions.assertTrue(recorded < 25, "the configured rate limit was ignored");
        }
    }

    @Test
    void forceFlush_CoversLogsAsWellAsTraces() {
        InMemorySpanExporter spans = InMemorySpanExporter.create();
        InMemoryLogRecordExporter logs = InMemoryLogRecordExporter.create();
        OpenTelemetrySdk sdk = OpenTelemetrySdk.builder()
                .setTracerProvider(SdkTracerProvider.builder()
                        .addSpanProcessor(BatchSpanProcessor.builder(spans).setScheduleDelay(Duration.ofHours(1)).build())
                        .build())
                .setLoggerProvider(SdkLoggerProvider.builder()
                        .addLogRecordProcessor(BatchLogRecordProcessor.builder(logs).setScheduleDelay(Duration.ofHours(1)).build())
                        .build())
                .build();
        GraftCodeAppInsightsConnectorOptions options = new GraftCodeAppInsightsConnectorOptions();
        options.setServiceName("test.service");
        options.setEnableConsoleLogging(false);

        try (GraftCodeAppInsightsConnectorPipeline pipeline = new GraftCodeAppInsightsConnectorPipeline(options, sdk)) {
            Span span = pipeline.startSpan("test.operation");
            try (Scope ignored = span.makeCurrent()) {
                pipeline.getLogger().info("Test log {Value}", 42);
            } finally {
                span.end();
            }

            Assertions.assertTrue(spans.getFinishedSpanItems().isEmpty());
            Assertions.assertTrue(logs.getFinishedLogRecordItems().isEmpty());

            Assertions.assertTrue(pipeline.forceFlush(10000));

            Assertions.assertEquals(1, spans.getFinishedSpanItems().size());
            Assertions.assertEquals(1, logs.getFinishedLogRecordItems().size());
        }
    }

    @Test
    void close_IsIdempotent_AndFlushAfterCloseSucceeds() {
        GraftCodeAppInsightsConnectorPipeline pipeline = startLocal();
        pipeline.close();
        pipeline.close();

        Assertions.assertTrue(pipeline.forceFlush());
    }

    private static GraftCodeAppInsightsConnectorPipeline startLocal() {
        GraftCodeAppInsightsConnectorOptions options = new GraftCodeAppInsightsConnectorOptions();
        options.setServiceName("test.service");
        options.setEnableConsoleLogging(false);
        options.setEnvironment(name -> null);
        return GraftCodeAppInsightsConnector.start(options);
    }

    private static GraftCodeAppInsightsConnectorPipeline startExporting(Double tracesPerSecond) {
        return GraftCodeAppInsightsConnector.start(options -> {
            options.setServiceName("test.service");
            options.setEnableConsoleLogging(false);
            options.setConnectionString(LOCAL_CONNECTION_STRING);
            options.setTracesPerSecond(tracesPerSecond);
        });
    }
}
