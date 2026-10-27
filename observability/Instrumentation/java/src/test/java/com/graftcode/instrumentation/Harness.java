package com.graftcode.instrumentation;

import com.hypertube.utils.Command;
import com.hypertube.utils.CommandType;
import com.hypertube.utils.RuntimeName;
import com.hypertube.utils.abstractions.HypertubeCallContext;
import com.hypertube.utils.connectiondata.IConnectionData;
import com.hypertube.utils.connectiondata.InMemoryConnectionData;
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
import io.opentelemetry.sdk.trace.samplers.Sampler;

import java.util.Collections;
import java.util.List;
import java.util.Map;

final class Harness implements AutoCloseable {
    static final Map<String, String> NO_HEADERS = Collections.emptyMap();

    private final InMemorySpanExporter spans = InMemorySpanExporter.create();
    private final InMemoryLogRecordExporter logs = InMemoryLogRecordExporter.create();
    private final OpenTelemetrySdk sdk;
    private final boolean autoInject = GraftcodeTelemetrySettings.isAutoInjectEnabled();
    private final boolean captureBodies = GraftcodeTelemetrySettings.isBodyCaptureEnabled();
    private final int bodyMaxCharacters = GraftcodeTelemetrySettings.getBodyMaxCharacters();
    final GraftcodeInstrumentation listener;

    Harness() {
        this(true, null);
    }

    Harness(boolean recording, LogsLevel minimumLogLevel) {
        sdk = OpenTelemetrySdk.builder()
                .setTracerProvider(SdkTracerProvider.builder()
                        .setSampler(recording ? Sampler.alwaysOn() : Sampler.alwaysOff())
                        .addSpanProcessor(SimpleSpanProcessor.create(spans))
                        .build())
                .setLoggerProvider(SdkLoggerProvider.builder()
                        .addLogRecordProcessor(SimpleLogRecordProcessor.create(logs))
                        .build())
                .build();
        listener = minimumLogLevel == null
                ? GraftcodeInstrumentation.attach(sdk)
                : GraftcodeInstrumentation.attach(sdk, minimumLogLevel);
    }

    CallSpan onInvoke(Command command, IConnectionData connectionData) {
        HypertubeCallContext context = new HypertubeCallContext();
        listener.onExecute(context, command, connectionData);
        return context.getItems().isEmpty() ? null : (CallSpan) context.getItems().get(0);
    }

    CallSpan onHandle(Command command, Map<String, String> headers) {
        HypertubeCallContext context = new HypertubeCallContext();
        Command handled = command;
        if (!headers.isEmpty()) {
            Object[] pairs = new Object[headers.size() * 2];
            int i = 0;
            for (Map.Entry<String, String> header : headers.entrySet()) {
                pairs[i++] = header.getKey();
                pairs[i++] = header.getValue();
            }

            handled = new Command(
                    command.getRuntimeName(),
                    CommandType.CONTEXT_METADATA,
                    new Command(command.getRuntimeName(), CommandType.ARRAY, pairs),
                    command);
        }

        listener.onProcess(context, handled);
        return context.getItems().isEmpty() ? null : (CallSpan) context.getItems().get(0);
    }

    static void complete(CallSpan span, Command response, Throwable exception) {
        if (span != null) {
            span.complete(response, exception);
        }
    }

    List<SpanData> stopped() {
        return spans.getFinishedSpanItems();
    }

    List<LogRecordData> logs() {
        return logs.getFinishedLogRecordItems();
    }

    @Override
    public void close() {
        listener.close();
        sdk.close();
        GraftcodeTelemetrySettings.setAutoInjectEnabled(autoInject);
        GraftcodeTelemetrySettings.setBodyCaptureEnabled(captureBodies);
        GraftcodeTelemetrySettings.setBodyMaxCharacters(bodyMaxCharacters);
        new InMemoryConnectionData().setThreadLocalHeaders(Collections.<String, String>emptyMap());
    }

    static final class Commands {
        private Commands() {
        }

        static Command of(CommandType type, Object... payload) {
            return new Command(RuntimeName.Jvm, type, payload);
        }

        static Command value(Object value) {
            return new Command(RuntimeName.Jvm, CommandType.VALUE, new Object[]{value});
        }

        static Command fetch() {
            return of(CommandType.INVOKE_STATIC_METHOD, of(CommandType.GET_TYPE, value("SomeLib.Service")), value("Fetch"));
        }
    }
}
