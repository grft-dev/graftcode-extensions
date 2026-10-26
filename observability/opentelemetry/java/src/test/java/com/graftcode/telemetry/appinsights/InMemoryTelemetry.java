package com.graftcode.telemetry.appinsights;

import com.hypertube.utils.logging.LogsLevel;
import io.opentelemetry.sdk.OpenTelemetrySdk;
import io.opentelemetry.sdk.logs.SdkLoggerProvider;
import io.opentelemetry.sdk.logs.data.LogRecordData;
import io.opentelemetry.sdk.logs.export.SimpleLogRecordProcessor;
import io.opentelemetry.sdk.testing.exporter.InMemoryLogRecordExporter;
import io.opentelemetry.sdk.testing.exporter.InMemorySpanExporter;
import io.opentelemetry.sdk.trace.SdkTracerProvider;
import io.opentelemetry.sdk.trace.data.SpanData;
import io.opentelemetry.sdk.trace.export.SimpleSpanProcessor;

import java.util.List;

final class InMemoryTelemetry implements AutoCloseable {
    private final InMemorySpanExporter spans = InMemorySpanExporter.create();
    private final InMemoryLogRecordExporter logs = InMemoryLogRecordExporter.create();
    final GraftAppInsightsConnectorPipeline pipeline;

    InMemoryTelemetry(LogsLevel minimumLogLevel) {
        GraftAppInsightsConnectorOptions options = new GraftAppInsightsConnectorOptions();
        options.setServiceName("TestService");
        options.setEnableConsoleLogging(false);
        options.setMinimumLogLevel(minimumLogLevel);

        OpenTelemetrySdk sdk = OpenTelemetrySdk.builder()
                .setTracerProvider(SdkTracerProvider.builder().addSpanProcessor(SimpleSpanProcessor.create(spans)).build())
                .setLoggerProvider(SdkLoggerProvider.builder().addLogRecordProcessor(SimpleLogRecordProcessor.create(logs)).build())
                .build();

        pipeline = new GraftAppInsightsConnectorPipeline(options, sdk);
    }

    List<SpanData> spans() {
        return spans.getFinishedSpanItems();
    }

    List<LogRecordData> logs() {
        return logs.getFinishedLogRecordItems();
    }

    @Override
    public void close() {
        pipeline.close();
    }
}
