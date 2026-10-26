package com.graftcode.instrumentation;

import com.hypertube.utils.Command;
import com.hypertube.utils.abstractions.HypertubeBroker;
import com.hypertube.utils.abstractions.HypertubeCallContext;
import com.hypertube.utils.abstractions.IHypertubeHandling;
import com.hypertube.utils.abstractions.IHypertubeInvocation;
import com.hypertube.utils.abstractions.IHypertubeLogging;
import com.hypertube.utils.connectiondata.IConnectionData;
import com.hypertube.utils.logging.HypertubeLogger;
import com.hypertube.utils.logging.LogsCommandResolver;
import com.hypertube.utils.logging.LogsLevel;
import com.hypertube.utils.logging.LogsRedactor;
import io.opentelemetry.api.OpenTelemetry;
import io.opentelemetry.api.common.AttributeKey;
import io.opentelemetry.api.common.Attributes;
import io.opentelemetry.api.common.AttributesBuilder;
import io.opentelemetry.api.logs.LogRecordBuilder;
import io.opentelemetry.api.logs.Logger;
import io.opentelemetry.api.logs.Severity;
import io.opentelemetry.api.trace.Span;
import io.opentelemetry.api.trace.Tracer;
import io.opentelemetry.context.Context;

import java.io.PrintWriter;
import java.io.StringWriter;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Objects;

public final class GraftcodeInstrumentation
        implements IHypertubeInvocation, IHypertubeHandling, IHypertubeLogging, AutoCloseable {
    public static final String INSTRUMENTATION_NAME = "Graftcode";
    public static final String LOGGER_NAME = "Hypertube";

    private final Tracer tracer;
    private final Logger logger;
    private final LogsLevel minimumLogLevel;

    private GraftcodeInstrumentation(OpenTelemetry openTelemetry, LogsLevel minimumLogLevel) {
        Objects.requireNonNull(openTelemetry, "openTelemetry");
        this.tracer = openTelemetry.getTracer(INSTRUMENTATION_NAME);
        this.minimumLogLevel = minimumLogLevel;
        this.logger = minimumLogLevel == null ? null : openTelemetry.getLogsBridge().get(LOGGER_NAME);
        HypertubeBroker.attach(this);
        HypertubeLogger.attach(this);
    }

    public static GraftcodeInstrumentation attach(OpenTelemetry openTelemetry) {
        return new GraftcodeInstrumentation(openTelemetry, null);
    }

    public static GraftcodeInstrumentation attach(OpenTelemetry openTelemetry, LogsLevel minimumLogLevel) {
        return new GraftcodeInstrumentation(openTelemetry, Objects.requireNonNull(minimumLogLevel, "minimumLogLevel"));
    }

    @Override
    public void onExecute(HypertubeCallContext context, Command command, IConnectionData connectionData) {
        Command reported = LogsCommandResolver.resolveForInvoke(command);
        CallSpan span = CallSpan.startInvoke(tracer, reported, connectionData);
        if (span != null) {
            context.getItems().add(span);
        }
    }

    @Override
    public void onResponse(HypertubeCallContext context, Command response) {
        completeSpans(context, response, null);
    }

    @Override
    public void onInvocationError(HypertubeCallContext context, Throwable exception) {
        completeSpans(context, null, exception);
    }

    @Override
    public void onProcess(HypertubeCallContext context, Command command) {
        Map<String, String> headers = new HashMap<>();
        Command reported = LogsCommandResolver.resolveForHandle(command, headers);
        CallSpan span = CallSpan.startHandle(tracer, reported, headers);
        if (span != null) {
            context.getItems().add(span);
        }
    }

    @Override
    public void onProcessed(HypertubeCallContext context, Command response) {
        completeSpans(context, response, null);
    }

    @Override
    public void onHandlingError(HypertubeCallContext context, Throwable exception) {
        completeSpans(context, null, exception);
    }

    @Override
    public boolean isRequested() {
        if (logger == null) {
            return Span.current().isRecording();
        }

        return minimumLogLevel != LogsLevel.Off;
    }

    boolean isLogEnabled(LogsLevel level) {
        if (logger == null) {
            return Span.current().isRecording();
        }

        return level != LogsLevel.Off && minimumLogLevel != LogsLevel.Off && level.isAtLeast(minimumLogLevel);
    }

    @Override
    public void onLog(LogsLevel level, String message, Map<String, String> properties, Throwable exception) {
        if (logger == null) {
            recordSpanEvent(level, message, properties, exception);
            return;
        }

        if (!isLogEnabled(level)) {
            return;
        }

        LogRecordBuilder record = logger.logRecordBuilder()
                .setContext(Context.current())
                .setSeverity(toSeverity(level))
                .setSeverityText(toLevelName(level))
                .setBody(message);

        for (Map.Entry<String, String> property : properties.entrySet()) {
            record.setAttribute(AttributeKey.stringKey(property.getKey()), property.getValue());
        }

        if (exception != null) {
            record.setAttribute(AttributeKey.stringKey("exception.type"), exception.getClass().getName());
            setIfPresent(record, "exception.message", LogsRedactor.redactValue(exception.getMessage()));
            setIfPresent(record, "exception.stacktrace", LogsRedactor.redactValue(stackTrace(exception)));
        }

        record.emit();
    }

    @Override
    public void close() {
        HypertubeBroker.detach(this);
        HypertubeLogger.detach(this);
    }

    private static void completeSpans(HypertubeCallContext context, Command response, Throwable exception) {
        List<Object> items = context.getItems();
        for (int i = items.size() - 1; i >= 0; i--) {
            Object item = items.get(i);
            if (item instanceof CallSpan) {
                ((CallSpan) item).complete(response, exception);
                items.remove(i);
            }
        }
    }

    private static void recordSpanEvent(LogsLevel level, String message, Map<String, String> properties, Throwable exception) {
        Span span = Span.current();
        if (!span.isRecording()) {
            return;
        }

        AttributesBuilder attributes = Attributes.builder().put("log.level", toLevelName(level));
        for (Map.Entry<String, String> property : properties.entrySet()) {
            attributes.put(property.getKey(), property.getValue());
        }

        if (exception != null) {
            attributes.put("log.message", message);
            attributes.put("exception.type", exception.getClass().getName());
            putIfPresent(attributes, "exception.message", LogsRedactor.redactValue(exception.getMessage()));
            putIfPresent(attributes, "exception.stacktrace", LogsRedactor.redactValue(stackTrace(exception)));
        }

        span.addEvent(exception == null ? message : "exception", attributes.build());
    }

    private static String stackTrace(Throwable exception) {
        StringWriter writer = new StringWriter();
        exception.printStackTrace(new PrintWriter(writer));
        return writer.toString();
    }

    private static void setIfPresent(LogRecordBuilder record, String key, String value) {
        if (value != null) {
            record.setAttribute(AttributeKey.stringKey(key), value);
        }
    }

    private static void putIfPresent(AttributesBuilder attributes, String key, String value) {
        if (value != null) {
            attributes.put(key, value);
        }
    }

    private static Severity toSeverity(LogsLevel level) {
        switch (level) {
            case Trace:
                return Severity.TRACE;
            case Debug:
                return Severity.DEBUG;
            case Info:
                return Severity.INFO;
            case Warning:
                return Severity.WARN;
            case Error:
                return Severity.ERROR;
            default:
                return Severity.UNDEFINED_SEVERITY_NUMBER;
        }
    }

    private static String toLevelName(LogsLevel level) {
        return level == LogsLevel.Info ? "Information" : level.name();
    }
}
