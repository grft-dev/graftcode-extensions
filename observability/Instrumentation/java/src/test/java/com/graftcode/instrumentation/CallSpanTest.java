package com.graftcode.instrumentation;

import com.graftcode.instrumentation.Harness.Commands;
import com.hypertube.utils.CommandType;
import com.hypertube.utils.connectiondata.InMemoryConnectionData;
import com.hypertube.utils.logging.LogsRedactor;
import io.opentelemetry.api.common.AttributeKey;
import io.opentelemetry.api.trace.Span;
import io.opentelemetry.api.trace.StatusCode;
import io.opentelemetry.sdk.trace.data.SpanData;
import org.junit.jupiter.api.Assertions;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.EnumSource;

import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.file.Path;
import java.util.Collections;
import java.util.jar.Attributes;
import java.util.jar.JarOutputStream;
import java.util.jar.Manifest;

class CallSpanTest {
    private static final String TRACE_ID = "0af7651916cd43dd8448eb211c80319c";
    private static final String PARENT_SPAN_ID = "b7ad6b7169203331";
    private static final String TRACE_PARENT = "00-" + TRACE_ID + "-" + PARENT_SPAN_ID + "-01";

    @Test
    void invoke_OpensADependencySpanNamedAfterTheTarget() {
        try (Harness harness = new Harness()) {
            Harness.complete(harness.onInvoke(Commands.fetch(), new InMemoryConnectionData()), Commands.value("ok"), null);

            SpanData stopped = single(harness);
            Assertions.assertEquals(GraftcodeInstrumentation.INSTRUMENTATION_NAME, stopped.getInstrumentationScopeInfo().getName());
            Assertions.assertEquals("Graft SomeLib inmemory", stopped.getName());
            Assertions.assertEquals("Graft", attribute(stopped, CallSpan.DEPENDENCY_TYPE));
            Assertions.assertEquals("SomeLib inmemory", attribute(stopped, CallSpan.DEPENDENCY_TARGET));
            Assertions.assertEquals("SomeLib.Service.Fetch", attribute(stopped, "hypertube.operation"));
            Assertions.assertEquals("INVOKE_STATIC_METHOD", attribute(stopped, "hypertube.command_type"));
            Assertions.assertEquals("Jvm", attribute(stopped, "hypertube.runtime"));
            Assertions.assertEquals("IN_MEMORY", attribute(stopped, "hypertube.connection_type"));
            Assertions.assertEquals("inmemory", attribute(stopped, "hypertube.target"));
            Assertions.assertEquals(StatusCode.OK, stopped.getStatus().getStatusCode());
            Assertions.assertFalse(Span.current().getSpanContext().isValid());
        }
    }

    @Test
    void invoke_WritesTheSpanIntoTheHeaders_AndRestoresThem() {
        try (Harness harness = new Harness()) {
            InMemoryConnectionData connection = new InMemoryConnectionData();
            connection.setThreadLocalHeaders(Collections.singletonMap("tenant", "t1"));

            CallSpan scope = harness.onInvoke(Commands.fetch(), connection);

            Assertions.assertEquals(traceParentOf(scope), connection.getHeaders().get(CallSpan.TRACE_PARENT_HEADER));
            Assertions.assertEquals(scope.getSpan().getSpanContext().getTraceId(), connection.getHeaders().get(CallSpan.TRACE_ID_HEADER));
            Assertions.assertEquals("t1", connection.getHeaders().get("tenant"));

            Harness.complete(scope, null, null);

            Assertions.assertFalse(connection.getHeaders().containsKey(CallSpan.TRACE_PARENT_HEADER));
            Assertions.assertFalse(connection.getHeaders().containsKey(CallSpan.TRACE_ID_HEADER));
            Assertions.assertEquals("t1", connection.getHeaders().get("tenant"));
        }
    }

    @Test
    void invoke_DoesNotReplaceATraceTheCallerAlreadySet() {
        try (Harness harness = new Harness()) {
            InMemoryConnectionData connection = new InMemoryConnectionData();
            connection.setHeaders(Collections.singletonMap(CallSpan.TRACE_PARENT_HEADER, TRACE_PARENT));

            CallSpan scope = harness.onInvoke(Commands.fetch(), connection);

            Assertions.assertEquals(TRACE_ID, scope.getSpan().getSpanContext().getTraceId());
            Assertions.assertEquals(TRACE_PARENT, connection.getHeaders().get(CallSpan.TRACE_PARENT_HEADER));
            Harness.complete(scope, null, null);
            Assertions.assertEquals(PARENT_SPAN_ID, single(harness).getParentSpanId());
        }
    }

    @Test
    void nestedInvoke_OnTheSameConnection_PropagatesTheInnerSpan_AndRestoresTheOuter() {
        try (Harness harness = new Harness()) {
            InMemoryConnectionData connection = new InMemoryConnectionData();

            CallSpan outer = harness.onInvoke(Commands.fetch(), connection);
            CallSpan inner = harness.onInvoke(Commands.fetch(), connection);

            Assertions.assertEquals(traceParentOf(inner), connection.getHeaders().get(CallSpan.TRACE_PARENT_HEADER));

            Harness.complete(inner, null, null);
            Assertions.assertEquals(traceParentOf(outer), connection.getHeaders().get(CallSpan.TRACE_PARENT_HEADER));
            Assertions.assertEquals(outer.getSpan().getSpanContext(), Span.current().getSpanContext());

            Harness.complete(outer, null, null);
            Assertions.assertFalse(connection.getHeaders().containsKey(CallSpan.TRACE_PARENT_HEADER));
            Assertions.assertFalse(Span.current().getSpanContext().isValid());
            Assertions.assertEquals(outer.getSpan().getSpanContext().getSpanId(), harness.stopped().get(0).getParentSpanId());
        }
    }

    @Test
    void withAutoInjectDisabled_HeadersAreLeftAlone() {
        try (Harness harness = new Harness()) {
            GraftcodeTelemetrySettings.setAutoInjectEnabled(false);
            InMemoryConnectionData connection = new InMemoryConnectionData();

            CallSpan scope = harness.onInvoke(Commands.fetch(), connection);

            Assertions.assertFalse(connection.getHeaders().containsKey(CallSpan.TRACE_PARENT_HEADER));
            Harness.complete(scope, null, null);
        }
    }

    @Test
    void failedCall_StopsTheSpanWithARedactedMessage() {
        try (Harness harness = new Harness()) {
            Harness.complete(
                    harness.onInvoke(Commands.fetch(), new InMemoryConnectionData()),
                    null,
                    new IllegalStateException("Password=hunter2"));

            SpanData stopped = single(harness);
            Assertions.assertEquals(StatusCode.ERROR, stopped.getStatus().getStatusCode());
            Assertions.assertEquals(LogsRedactor.PLACEHOLDER, stopped.getStatus().getDescription());
            Assertions.assertEquals(IllegalStateException.class.getName(), attribute(stopped, "exception.type"));
            Assertions.assertEquals(LogsRedactor.PLACEHOLDER, attribute(stopped, "exception.message"));
        }
    }

    @ParameterizedTest
    @EnumSource(value = CommandType.class, names = {"INVOKE_DELEGATE", "PASS_BY_VALUE", "REGISTER_FOR_UPDATE", "VALUE_FOR_UPDATE"})
    void transportCommands_DoNotOpenASpan(CommandType type) {
        try (Harness harness = new Harness()) {
            Assertions.assertNull(harness.onInvoke(Commands.of(type, "x"), new InMemoryConnectionData()));
            Assertions.assertNull(harness.onHandle(Commands.of(type, "x"), Harness.NO_HEADERS));
            Assertions.assertTrue(harness.stopped().isEmpty());
        }
    }

    @Test
    void telemetrySetupCalls_DoNotOpenASpan() {
        try (Harness harness = new Harness()) {
            Assertions.assertNull(harness.onHandle(
                    Commands.of(
                            CommandType.INVOKE_STATIC_METHOD,
                            Commands.of(CommandType.GET_TYPE, Commands.value("app.TelemetrySetup")),
                            Commands.value("initialize")),
                    Harness.NO_HEADERS));
        }
    }

    @Test
    void unrecordedSpan_GetsNoAttributes_ButStillPropagates() {
        try (Harness harness = new Harness(false, null)) {
            InMemoryConnectionData connection = new InMemoryConnectionData();

            CallSpan scope = harness.onInvoke(Commands.fetch(), connection);

            Assertions.assertFalse(scope.getSpan().isRecording());
            Assertions.assertEquals(traceParentOf(scope), connection.getHeaders().get(CallSpan.TRACE_PARENT_HEADER));
            Harness.complete(scope, null, null);
            Assertions.assertTrue(harness.stopped().isEmpty());
        }
    }

    @Test
    void handle_IsNamedAfterTheOperation_AndParentedOnTheIncomingTraceParent() {
        try (Harness harness = new Harness()) {
            CallSpan scope = harness.onHandle(Commands.fetch(), Collections.singletonMap(CallSpan.TRACE_PARENT_HEADER, TRACE_PARENT));
            Harness.complete(scope, null, null);

            SpanData stopped = single(harness);
            Assertions.assertEquals("SomeLib.Service.Fetch", stopped.getName());
            Assertions.assertEquals(TRACE_ID, stopped.getTraceId());
            Assertions.assertEquals(PARENT_SPAN_ID, stopped.getParentSpanId());
        }
    }

    @Test
    void handle_FallsBackToTheBareTraceId() {
        try (Harness harness = new Harness()) {
            CallSpan scope = harness.onHandle(Commands.fetch(), Collections.singletonMap(CallSpan.TRACE_ID_HEADER, TRACE_ID));
            Harness.complete(scope, null, null);

            Assertions.assertEquals(TRACE_ID, single(harness).getTraceId());
        }
    }

    @Test
    void loadedLibrary_NamesTheTargetOfLaterCalls(@TempDir Path directory) throws IOException {
        Path jar = directory.resolve("TestGgJavaLib.jar");
        Manifest manifest = new Manifest();
        manifest.getMainAttributes().put(Attributes.Name.MANIFEST_VERSION, "1.0");
        manifest.getMainAttributes().put(Attributes.Name.IMPLEMENTATION_VERSION, "1.2.3");
        new JarOutputStream(new FileOutputStream(jar.toFile()), manifest).close();

        try (Harness harness = new Harness()) {
            InMemoryConnectionData connection = new InMemoryConnectionData();

            Harness.complete(harness.onInvoke(Commands.of(CommandType.LOAD_LIBRARY, Commands.value(jar.toString())), connection), null, null);
            Harness.complete(harness.onInvoke(
                    Commands.of(
                            CommandType.INVOKE_STATIC_METHOD,
                            Commands.of(CommandType.GET_TYPE, Commands.value("testggjavalib.RandomService")),
                            Commands.value("fetch")),
                    connection), null, null);

            Assertions.assertEquals("Graft TestGgJavaLib 1.2.3 inmemory", harness.stopped().get(1).getName());
        }
    }

    private static SpanData single(Harness harness) {
        Assertions.assertEquals(1, harness.stopped().size());
        return harness.stopped().get(0);
    }

    private static String attribute(SpanData span, String key) {
        return span.getAttributes().get(AttributeKey.stringKey(key));
    }

    private static String traceParentOf(CallSpan span) {
        return "00-" + span.getSpan().getSpanContext().getTraceId()
                + "-" + span.getSpan().getSpanContext().getSpanId()
                + "-" + span.getSpan().getSpanContext().getTraceFlags().asHex();
    }
}
