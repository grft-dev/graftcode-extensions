from hypertube.utils.CommandType import CommandType
from hypertube.utils.connectionData.InMemoryConnectionData import InMemoryConnectionData
from hypertube.utils.logging.LogsRedactor import LogsRedactor

from graftcode_instrumentation import GraftcodeTelemetrySettings
from graftcode_instrumentation.BodyCapture import BodyCapture
from graftcode_instrumentation.CallSpan import CallSpan

from Harness import Commands, Harness, NO_HEADERS


class HostileDto:
    def __str__(self):
        raise RuntimeError("__str__ exploded")


def capturing(max_characters=None):
    harness = Harness()
    GraftcodeTelemetrySettings.set_body_capture_enabled(True)
    if max_characters is not None:
        GraftcodeTelemetrySettings.set_body_max_characters(max_characters)
    return harness


def invoke(harness, command, response):
    Harness.complete(harness.on_invoke(command, InMemoryConnectionData()), response, None)


def tag(harness, key):
    return harness.stopped()[0].attributes.get(key)


def test_body_capture_is_disabled_by_default():
    assert not GraftcodeTelemetrySettings.is_body_capture_enabled()


def test_body_limit_is_clamped():
    assert GraftcodeTelemetrySettings.clamp_body_max_characters(1) == GraftcodeTelemetrySettings.MINIMUM_BODY_MAX_CHARS
    assert GraftcodeTelemetrySettings.clamp_body_max_characters(2 ** 31 - 1) == (
        GraftcodeTelemetrySettings.MAXIMUM_BODY_MAX_CHARS)


def test_with_capture_disabled_span_carries_no_body():
    with Harness() as harness:
        invoke(harness, Commands.fetch(), Commands.value("ok"))

        assert tag(harness, CallSpan.REQUEST_BODY) is None
        assert tag(harness, CallSpan.RESPONSE_BODY) is None


def test_with_capture_enabled_request_and_response_bodies_are_attached():
    with capturing() as harness:
        invoke(harness, Commands.fetch(), Commands.value("order-4711"))

        assert "Fetch" in tag(harness, CallSpan.REQUEST_BODY)
        assert "order-4711" in tag(harness, CallSpan.RESPONSE_BODY)


def test_with_capture_enabled_handle_span_carries_the_request_body():
    with capturing() as harness:
        Harness.complete(harness.on_handle(Commands.fetch(), NO_HEADERS), None, None)

        assert "Fetch" in tag(harness, CallSpan.REQUEST_BODY)


def test_with_capture_enabled_sensitive_values_are_redacted():
    with capturing() as harness:
        invoke(harness, Commands.of(CommandType.InvokeStaticMethod, "password", "hunter2", "note",
                                    "Server=x;Password=s3cr3t"), None)

        body = tag(harness, CallSpan.REQUEST_BODY)
        assert LogsRedactor.PLACEHOLDER in body
        assert "hunter2" not in body
        assert "s3cr3t" not in body


def test_oversized_payload_sets_the_truncation_flag():
    with capturing(256) as harness:
        invoke(harness, Commands.of(CommandType.InvokeStaticMethod, *(["y" * 64] * 64)), None)

        assert harness.stopped()[0].attributes.get(CallSpan.REQUEST_BODY_TRUNCATED) is True
        assert BodyCapture.TRUNCATION_MARKER in tag(harness, CallSpan.REQUEST_BODY)


def test_unrecorded_span_skips_body_rendering():
    with Harness(False, None) as harness:
        GraftcodeTelemetrySettings.set_body_capture_enabled(True)

        scope = harness.on_invoke(Commands.fetch(), InMemoryConnectionData())
        Harness.complete(scope, Commands.value("ok"), None)

        assert not scope.span.is_recording()
        assert harness.stopped() == []


def test_payload_with_a_throwing_str_is_rendered_as_a_placeholder():
    with capturing() as harness:
        invoke(harness, Commands.of(CommandType.InvokeStaticMethod, "Fetch", HostileDto()), None)

        body = tag(harness, CallSpan.REQUEST_BODY)
        assert "Fetch" in body
        assert "[unrenderable:" + HostileDto.__module__ + "." + HostileDto.__qualname__ + "]" in body


def test_byte_arrays_are_rendered_as_their_length():
    with capturing() as harness:
        invoke(harness, Commands.of(CommandType.InvokeStaticMethod, bytes(42)), None)

        assert "[bytes:42]" in tag(harness, CallSpan.REQUEST_BODY)
