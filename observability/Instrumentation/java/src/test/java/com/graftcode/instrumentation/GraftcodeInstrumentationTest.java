package com.graftcode.instrumentation;

import com.graftcode.instrumentation.Harness.Commands;
import com.hypertube.core.interpreter.Interpreter;
import com.hypertube.utils.Command;
import com.hypertube.utils.CommandType;
import com.hypertube.utils.RuntimeName;
import com.hypertube.utils.connectiondata.InMemoryConnectionData;
import com.hypertube.utils.logging.HypertubeLogger;
import com.hypertube.utils.logging.LogsLevel;
import com.hypertube.utils.logging.LogsRedactor;
import io.opentelemetry.api.common.AttributeKey;
import io.opentelemetry.api.logs.Severity;
import io.opentelemetry.sdk.logs.data.LogRecordData;
import io.opentelemetry.sdk.trace.data.EventData;
import io.opentelemetry.sdk.trace.data.SpanData;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.Assertions;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;

import java.util.Collections;
import java.util.List;

class GraftcodeInstrumentationTest {
    private final LogsLevel minimumLevel = HypertubeLogger.getMinimumLevel();

    @BeforeEach
    void silenceConsole() {
        HypertubeLogger.setMinimumLevel(LogsLevel.Off);
    }

    @AfterEach
    void restoreConsole() {
        HypertubeLogger.setMinimumLevel(minimumLevel);
    }

    @Test
    void attach_HooksIntoHypertube_AndCloseDetaches() {
        HypertubeLogger.setMinimumLevel(LogsLevel.Error);
        Harness harness = new Harness(true, LogsLevel.Info);
        Assertions.assertTrue(HypertubeLogger.isEnabled(LogsLevel.Info));

        harness.close();
        Assertions.assertFalse(HypertubeLogger.isEnabled(LogsLevel.Info));
    }

    @Test
    void withoutALogger_LogsBecomeSpanEvents_WithTheExceptionRedacted() {
        try (Harness harness = new Harness()) {
            CallSpan scope = harness.onHandle(Commands.fetch(), Harness.NO_HEADERS);
            Assertions.assertTrue(harness.listener.isLogEnabled(LogsLevel.Info));
            harness.listener.onLog(
                    LogsLevel.Info,
                    "Hypertube handling INVOKE_STATIC_METHOD",
                    Collections.singletonMap("CommandType", "INVOKE_STATIC_METHOD"),
                    null);
            harness.listener.onLog(LogsLevel.Error, "failed", Harness.NO_HEADERS, new IllegalStateException("Password=hunter2"));
            Harness.complete(scope, null, null);

            List<EventData> events = harness.stopped().get(0).getEvents();
            Assertions.assertEquals("Hypertube handling INVOKE_STATIC_METHOD", events.get(0).getName());
            Assertions.assertEquals("INVOKE_STATIC_METHOD", events.get(0).getAttributes().get(AttributeKey.stringKey("CommandType")));
            Assertions.assertEquals("Information", events.get(0).getAttributes().get(AttributeKey.stringKey("log.level")));
            Assertions.assertEquals("exception", events.get(1).getName());
            Assertions.assertEquals(LogsRedactor.PLACEHOLDER, events.get(1).getAttributes().get(AttributeKey.stringKey("exception.message")));
            events.get(1).getAttributes().forEach((key, value) -> Assertions.assertFalse(String.valueOf(value).contains("hunter2")));
        }
    }

    @Test
    void withALogger_LogsAreForwarded_InsteadOfRecordedOnTheSpan() {
        try (Harness harness = new Harness(true, LogsLevel.Trace)) {
            CallSpan scope = harness.onHandle(Commands.fetch(), Harness.NO_HEADERS);
            harness.listener.onLog(LogsLevel.Warning, "hello ada", Collections.singletonMap("Name", "ada"), null);
            Harness.complete(scope, null, null);

            Assertions.assertEquals(1, harness.logs().size());
            LogRecordData entry = harness.logs().get(0);
            Assertions.assertEquals(Severity.WARN, entry.getSeverity());
            Assertions.assertEquals("hello ada", entry.getBodyValue().asString());
            Assertions.assertEquals(scope.getSpan().getSpanContext().getSpanId(), entry.getSpanContext().getSpanId());
            Assertions.assertEquals("ada", entry.getAttributes().get(AttributeKey.stringKey("Name")));
            Assertions.assertTrue(harness.stopped().get(0).getEvents().isEmpty());
        }
    }

    @Test
    void loggerAtInformation_ReceivesHypertubeLines_AtItsLevelOnly() {
        try (Harness harness = new Harness(true, LogsLevel.Info)) {
            HypertubeLogger.getLogger().log(LogsLevel.Debug, "Hypertube debug {Detail}", "x");
            HypertubeLogger.getLogger().log(LogsLevel.Info, "Hypertube invoking {CommandType}", "INVOKE_STATIC_METHOD");

            Assertions.assertEquals(1, harness.logs().size());
            Assertions.assertEquals(Severity.INFO, harness.logs().get(0).getSeverity());
            Assertions.assertEquals("Hypertube invoking INVOKE_STATIC_METHOD", harness.logs().get(0).getBodyValue().asString());
        }
    }

    @Test
    void loggerLevel_DecidesWhatIsEnabled() {
        try (Harness harness = new Harness(true, LogsLevel.Error)) {
            Assertions.assertFalse(harness.listener.isLogEnabled(LogsLevel.Warning));
            Assertions.assertTrue(harness.listener.isLogEnabled(LogsLevel.Error));
        }
    }

    @Test
    void loggerOff_DisablesForwarding() {
        HypertubeLogger.setMinimumLevel(LogsLevel.Error);
        try (Harness harness = new Harness(true, LogsLevel.Off)) {
            Assertions.assertFalse(HypertubeLogger.isEnabled(LogsLevel.Info));
            Assertions.assertFalse(harness.listener.isLogEnabled(LogsLevel.Error));
        }
    }

    @Test
    void withoutAnExporter_LogsAreDisabled() {
        try (Harness harness = new Harness(false, null)) {
            CallSpan scope = harness.onHandle(Commands.fetch(), Harness.NO_HEADERS);

            Assertions.assertFalse(harness.listener.isLogEnabled(LogsLevel.Error));
            Harness.complete(scope, null, null);
        }
    }

    @Test
    void inMemoryCall_ProducesAnInvokeSpan_AndAHandleSpanInTheSameTrace() {
        try (Harness harness = new Harness(true, LogsLevel.Info)) {
            Command response = Interpreter.execute(
                    new Command(RuntimeName.Jvm, CommandType.INVOKE_STATIC_METHOD,
                            new Command(RuntimeName.Jvm, CommandType.GET_TYPE, "java.lang.Math"), "abs", -5),
                    new InMemoryConnectionData());

            Assertions.assertEquals(CommandType.VALUE, response.getCommandType());
            List<SpanData> spans = harness.stopped();
            Assertions.assertEquals(2, spans.size());
            SpanData handle = spans.get(0);
            SpanData invoke = spans.get(1);
            Assertions.assertEquals("java.lang.Math.abs", handle.getName());
            Assertions.assertEquals("Graft java inmemory", invoke.getName());
            Assertions.assertEquals(invoke.getTraceId(), handle.getTraceId());
            Assertions.assertEquals(invoke.getSpanId(), handle.getParentSpanId());

            List<LogRecordData> logs = harness.logs();
            Assertions.assertEquals(4, logs.size());
            Assertions.assertEquals("Hypertube invoking INVOKE_STATIC_METHOD on the Jvm runtime", logs.get(0).getBodyValue().asString());
            Assertions.assertEquals(invoke.getSpanId(), logs.get(0).getSpanContext().getSpanId());
            Assertions.assertEquals(handle.getSpanId(), logs.get(1).getSpanContext().getSpanId());
        }
    }
}
