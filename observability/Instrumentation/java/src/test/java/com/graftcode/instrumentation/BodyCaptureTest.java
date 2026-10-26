package com.graftcode.instrumentation;

import com.graftcode.instrumentation.Harness.Commands;
import com.hypertube.utils.Command;
import com.hypertube.utils.CommandType;
import com.hypertube.utils.connectiondata.InMemoryConnectionData;
import com.hypertube.utils.logging.LogsRedactor;
import io.opentelemetry.api.common.AttributeKey;
import org.junit.jupiter.api.Assertions;
import org.junit.jupiter.api.Test;

import java.util.Arrays;

class BodyCaptureTest {

    @Test
    void bodyCapture_IsDisabledByDefault() {
        Assertions.assertFalse(GraftcodeTelemetrySettings.isBodyCaptureEnabled());
    }

    @Test
    void bodyLimit_IsClamped() {
        Assertions.assertEquals(GraftcodeTelemetrySettings.MINIMUM_BODY_MAX_CHARS, GraftcodeTelemetrySettings.clampBodyMaxCharacters(1));
        Assertions.assertEquals(GraftcodeTelemetrySettings.MAXIMUM_BODY_MAX_CHARS, GraftcodeTelemetrySettings.clampBodyMaxCharacters(Integer.MAX_VALUE));
    }

    @Test
    void withCaptureDisabled_SpanCarriesNoBody() {
        try (Harness harness = new Harness()) {
            invoke(harness, Commands.fetch(), Commands.value("ok"));

            Assertions.assertNull(tag(harness, CallSpan.REQUEST_BODY));
            Assertions.assertNull(tag(harness, CallSpan.RESPONSE_BODY));
        }
    }

    @Test
    void withCaptureEnabled_RequestAndResponseBodiesAreAttached() {
        try (Harness harness = capturing(null)) {
            invoke(harness, Commands.fetch(), Commands.value("order-4711"));

            Assertions.assertTrue(tag(harness, CallSpan.REQUEST_BODY).contains("Fetch"));
            Assertions.assertTrue(tag(harness, CallSpan.RESPONSE_BODY).contains("order-4711"));
        }
    }

    @Test
    void withCaptureEnabled_HandleSpanCarriesTheRequestBody() {
        try (Harness harness = capturing(null)) {
            Harness.complete(harness.onHandle(Commands.fetch(), Harness.NO_HEADERS), null, null);

            Assertions.assertTrue(tag(harness, CallSpan.REQUEST_BODY).contains("Fetch"));
        }
    }

    @Test
    void withCaptureEnabled_SensitiveValuesAreRedacted() {
        try (Harness harness = capturing(null)) {
            invoke(harness, Commands.of(CommandType.INVOKE_STATIC_METHOD, "password", "hunter2", "note", "Server=x;Password=s3cr3t"), null);

            String body = tag(harness, CallSpan.REQUEST_BODY);
            Assertions.assertTrue(body.contains(LogsRedactor.PLACEHOLDER));
            Assertions.assertFalse(body.contains("hunter2"));
            Assertions.assertFalse(body.contains("s3cr3t"));
        }
    }

    @Test
    void oversizedPayload_SetsTheTruncationFlag() {
        try (Harness harness = capturing(256)) {
            Object[] payload = new Object[64];
            char[] filler = new char[64];
            Arrays.fill(filler, 'y');
            Arrays.fill(payload, new String(filler));

            invoke(harness, Commands.of(CommandType.INVOKE_STATIC_METHOD, payload), null);

            Assertions.assertEquals(Boolean.TRUE, harness.stopped().get(0).getAttributes().get(AttributeKey.booleanKey(CallSpan.REQUEST_BODY_TRUNCATED)));
            Assertions.assertTrue(tag(harness, CallSpan.REQUEST_BODY).contains(BodyCapture.TRUNCATION_MARKER));
        }
    }

    @Test
    void unrecordedSpan_SkipsBodyRendering() {
        try (Harness harness = new Harness(false, null)) {
            GraftcodeTelemetrySettings.setBodyCaptureEnabled(true);

            CallSpan scope = harness.onInvoke(Commands.fetch(), new InMemoryConnectionData());
            Harness.complete(scope, Commands.value("ok"), null);

            Assertions.assertFalse(scope.getSpan().isRecording());
            Assertions.assertTrue(harness.stopped().isEmpty());
        }
    }

    @Test
    void payloadWithAThrowingToString_IsRenderedAsAPlaceholder() {
        try (Harness harness = capturing(null)) {
            invoke(harness, Commands.of(CommandType.INVOKE_STATIC_METHOD, "Fetch", new HostileDto()), null);

            String body = tag(harness, CallSpan.REQUEST_BODY);
            Assertions.assertTrue(body.contains("Fetch"));
            Assertions.assertTrue(body.contains("[unrenderable:" + HostileDto.class.getName() + "]"));
        }
    }

    @Test
    void byteArrays_AreRenderedAsTheirLength() {
        try (Harness harness = capturing(null)) {
            invoke(harness, Commands.of(CommandType.INVOKE_STATIC_METHOD, new Object[]{new byte[42]}), null);

            Assertions.assertTrue(tag(harness, CallSpan.REQUEST_BODY).contains("[bytes:42]"));
        }
    }

    private static final class HostileDto {
        @Override
        public String toString() {
            throw new IllegalStateException("toString exploded");
        }
    }

    private static Harness capturing(Integer maxCharacters) {
        Harness harness = new Harness();
        GraftcodeTelemetrySettings.setBodyCaptureEnabled(true);
        if (maxCharacters != null) {
            GraftcodeTelemetrySettings.setBodyMaxCharacters(maxCharacters);
        }

        return harness;
    }

    private static void invoke(Harness harness, Command command, Command response) {
        Harness.complete(harness.onInvoke(command, new InMemoryConnectionData()), response, null);
    }

    private static String tag(Harness harness, String key) {
        return harness.stopped().get(0).getAttributes().get(AttributeKey.stringKey(key));
    }
}
