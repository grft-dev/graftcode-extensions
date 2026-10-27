package com.graftcode.instrumentation;

import com.hypertube.utils.CommandType;
import com.hypertube.utils.ICommand;
import com.hypertube.utils.connectiondata.IConnectionData;
import com.hypertube.utils.logging.LogsRedactor;
import io.opentelemetry.api.trace.Span;
import io.opentelemetry.api.trace.SpanBuilder;
import io.opentelemetry.api.trace.SpanContext;
import io.opentelemetry.api.trace.SpanId;
import io.opentelemetry.api.trace.SpanKind;
import io.opentelemetry.api.trace.StatusCode;
import io.opentelemetry.api.trace.TraceFlags;
import io.opentelemetry.api.trace.TraceState;
import io.opentelemetry.api.trace.Tracer;
import io.opentelemetry.api.trace.propagation.W3CTraceContextPropagator;
import io.opentelemetry.context.Context;
import io.opentelemetry.context.Scope;
import io.opentelemetry.context.propagation.TextMapGetter;

import java.util.Collections;
import java.util.HashMap;
import java.util.Locale;
import java.util.Map;
import java.util.concurrent.ThreadLocalRandom;

final class CallSpan implements AutoCloseable {
    static final String DEPENDENCY_TYPE = "graft.dependency.type";
    static final String DEPENDENCY_TARGET = "graft.dependency.target";
    static final String BRIDGE_DEPENDENCY_TYPE = "Graft";
    static final String REQUEST_BODY = "hypertube.request.body";
    static final String RESPONSE_BODY = "hypertube.response.body";
    static final String REQUEST_BODY_TRUNCATED = "hypertube.request.body.truncated";
    static final String RESPONSE_BODY_TRUNCATED = "hypertube.response.body.truncated";
    static final String TRACE_PARENT_HEADER = "traceparent";
    static final String TRACE_STATE_HEADER = "tracestate";
    static final String TRACE_ID_HEADER = "traceid";

    private static final TextMapGetter<Map<String, String>> HEADER_GETTER = new TextMapGetter<Map<String, String>>() {
        @Override
        public Iterable<String> keys(Map<String, String> carrier) {
            return carrier.keySet();
        }

        @Override
        public String get(Map<String, String> carrier, String key) {
            return header(carrier, key);
        }
    };

    private final Span span;
    private final Scope scope;
    private IConnectionData injectedInto;
    private Map<String, String> previousHeaders;
    private boolean completed;

    private CallSpan(Span span) {
        this.span = span;
        this.scope = span.makeCurrent();
    }

    Span getSpan() {
        return span;
    }

    static CallSpan startInvoke(Tracer tracer, ICommand command, IConnectionData connectionData) {
        String operation = resolveOperation(command);
        if (operation == null) {
            return null;
        }

        LibraryCatalog.record(command, connectionData);
        String target = LibraryCatalog.describeTarget(command, connectionData);
        Span span = start(tracer, "Graft " + target, connectionData.getHeaders());

        if (span.isRecording()) {
            span.setAttribute("hypertube.runtime", String.valueOf(command.getRuntimeName()));
            span.setAttribute("hypertube.command_type", String.valueOf(command.getCommandType()));
            span.setAttribute("hypertube.connection_type", String.valueOf(connectionData.getConnectionType()));
            span.setAttribute(DEPENDENCY_TYPE, BRIDGE_DEPENDENCY_TYPE);
            setIfPresent(span, DEPENDENCY_TARGET, LogsRedactor.redactValue(target));
            setIfPresent(span, "hypertube.operation", LogsRedactor.redactValue(operation));
            setIfPresent(span, "hypertube.target", LogsRedactor.redactValue(LogsRedactor.redactTarget(connectionData.getHostname())));
        }

        CallSpan callSpan = new CallSpan(span);
        callSpan.capture(command, REQUEST_BODY, REQUEST_BODY_TRUNCATED);
        if (GraftcodeTelemetrySettings.isAutoInjectEnabled()) {
            callSpan.injectInto(connectionData);
        }

        return callSpan;
    }

    static CallSpan startHandle(Tracer tracer, ICommand command, Map<String, String> headers) {
        String operation = resolveOperation(command);
        if (operation == null) {
            return null;
        }

        CallSpan callSpan = new CallSpan(start(tracer, operation, headers));
        callSpan.capture(command, REQUEST_BODY, REQUEST_BODY_TRUNCATED);
        return callSpan;
    }

    void complete(ICommand response, Throwable exception) {
        if (completed) {
            return;
        }

        completed = true;
        try {
            if (response != null) {
                capture(response, RESPONSE_BODY, RESPONSE_BODY_TRUNCATED);
            }

            if (exception == null) {
                span.setStatus(StatusCode.OK);
            } else {
                String message = LogsRedactor.redactValue(exception.getMessage());
                span.setAttribute("exception.type", exception.getClass().getName());
                setIfPresent(span, "exception.message", message);
                span.setStatus(StatusCode.ERROR, message == null ? "" : message);
            }

            if (injectedInto != null) {
                injectedInto.setThreadLocalHeaders(previousHeaders);
            }
        } finally {
            scope.close();
            span.end();
        }
    }

    @Override
    public void close() {
        complete(null, null);
    }

    private static String resolveOperation(ICommand command) {
        CommandType type = command.getCommandType();
        if (type == CommandType.PASS_BY_VALUE
                || type == CommandType.REGISTER_FOR_UPDATE
                || type == CommandType.VALUE_FOR_UPDATE
                || type == CommandType.INVOKE_DELEGATE) {
            return null;
        }

        String operation = OperationName.resolve(command);
        return operation.toLowerCase(Locale.ROOT).endsWith("telemetrysetup.initialize") ? null : operation;
    }

    private static Span start(Tracer tracer, String name, Map<String, String> headers) {
        SpanBuilder builder = tracer.spanBuilder(name).setSpanKind(SpanKind.INTERNAL);
        Map<String, String> carrier = headers != null ? headers : Collections.<String, String>emptyMap();

        Context remote = W3CTraceContextPropagator.getInstance().extract(Context.root(), carrier, HEADER_GETTER);
        if (Span.fromContext(remote).getSpanContext().isValid()) {
            return builder.setParent(remote).startSpan();
        }

        String traceId = readTraceId(header(carrier, TRACE_ID_HEADER));
        if (traceId != null && !traceId.equals(Span.current().getSpanContext().getTraceId())) {
            SpanContext parent = SpanContext.createFromRemoteParent(
                    traceId,
                    randomSpanId(),
                    TraceFlags.getSampled(),
                    TraceState.getDefault());
            return builder.setParent(Context.root().with(Span.wrap(parent))).startSpan();
        }

        return builder.startSpan();
    }

    private static String header(Map<String, String> headers, String key) {
        String value = headers.get(key);
        if (value != null) {
            return value;
        }

        for (Map.Entry<String, String> entry : headers.entrySet()) {
            if (key.equalsIgnoreCase(entry.getKey())) {
                return entry.getValue();
            }
        }

        return null;
    }

    private static String readTraceId(String raw) {
        if (raw == null) {
            return null;
        }

        String hex = raw.trim().replace("-", "").toLowerCase(Locale.ROOT);
        if (hex.length() != 32 || hex.replace("0", "").isEmpty()) {
            return null;
        }

        for (int i = 0; i < hex.length(); i++) {
            char character = hex.charAt(i);
            if ((character < '0' || character > '9') && (character < 'a' || character > 'f')) {
                return null;
            }
        }

        return hex;
    }

    private static String randomSpanId() {
        long id;
        do {
            id = ThreadLocalRandom.current().nextLong();
        } while (id == 0);

        return SpanId.fromLong(id);
    }

    private void injectInto(IConnectionData connectionData) {
        Map<String, String> local = connectionData.getThreadLocalHeaders();
        SpanContext spanContext = span.getSpanContext();
        if (!spanContext.isValid()
                || (connectionData.getHeaders().containsKey(TRACE_PARENT_HEADER) && !local.containsKey(TRACE_PARENT_HEADER))) {
            return;
        }

        Map<String, String> headers = new HashMap<>(local);
        headers.remove(TRACE_STATE_HEADER);
        W3CTraceContextPropagator.getInstance().inject(Context.root().with(span), headers, Map::put);
        headers.put(TRACE_ID_HEADER, spanContext.getTraceId());

        connectionData.setThreadLocalHeaders(headers);
        injectedInto = connectionData;
        previousHeaders = local;
    }

    private void capture(ICommand command, String bodyKey, String truncatedKey) {
        if (!span.isRecording() || !GraftcodeTelemetrySettings.isBodyCaptureEnabled()) {
            return;
        }

        BodyCapture.Result result = BodyCapture.render(command, GraftcodeTelemetrySettings.getBodyMaxCharacters());
        if (result == null || result.body == null) {
            return;
        }

        span.setAttribute(bodyKey, result.body);
        if (result.truncated) {
            span.setAttribute(truncatedKey, true);
        }
    }

    private static void setIfPresent(Span span, String key, String value) {
        if (value != null) {
            span.setAttribute(key, value);
        }
    }
}
