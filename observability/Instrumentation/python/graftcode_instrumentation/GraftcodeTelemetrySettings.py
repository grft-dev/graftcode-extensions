import os


def _read_flag(name, default_value):
    value = os.environ.get(name)
    if value is None or not value.strip():
        return default_value

    return value.strip().lower() != "false" and value.strip() != "0"


class GraftcodeTelemetrySettings:
    MINIMUM_BODY_MAX_CHARS = 256
    DEFAULT_BODY_MAX_CHARS = 8192
    MAXIMUM_BODY_MAX_CHARS = 32768

    _auto_inject = _read_flag("HYPERTUBE_TELEMETRY_AUTO_INJECT", True)
    _capture_bodies = _read_flag("HYPERTUBE_TELEMETRY_CAPTURE_BODIES", False)
    _body_max_characters = None

    @staticmethod
    def is_auto_inject_enabled():
        return GraftcodeTelemetrySettings._auto_inject

    @staticmethod
    def set_auto_inject_enabled(value):
        GraftcodeTelemetrySettings._auto_inject = bool(value)

    @staticmethod
    def is_body_capture_enabled():
        return GraftcodeTelemetrySettings._capture_bodies

    @staticmethod
    def set_body_capture_enabled(value):
        GraftcodeTelemetrySettings._capture_bodies = bool(value)

    @staticmethod
    def get_body_max_characters():
        return GraftcodeTelemetrySettings._body_max_characters

    @staticmethod
    def set_body_max_characters(value):
        GraftcodeTelemetrySettings._body_max_characters = GraftcodeTelemetrySettings.clamp_body_max_characters(value)

    @staticmethod
    def clamp_body_max_characters(value):
        if value < GraftcodeTelemetrySettings.MINIMUM_BODY_MAX_CHARS:
            return GraftcodeTelemetrySettings.MINIMUM_BODY_MAX_CHARS

        return min(value, GraftcodeTelemetrySettings.MAXIMUM_BODY_MAX_CHARS)

    @staticmethod
    def _read_body_max_chars():
        raw = os.environ.get("HYPERTUBE_TELEMETRY_BODY_MAX_CHARS")
        if raw is None or not raw.strip():
            return GraftcodeTelemetrySettings.DEFAULT_BODY_MAX_CHARS

        try:
            return GraftcodeTelemetrySettings.clamp_body_max_characters(int(raw.strip()))
        except ValueError:
            return GraftcodeTelemetrySettings.DEFAULT_BODY_MAX_CHARS


GraftcodeTelemetrySettings._body_max_characters = GraftcodeTelemetrySettings._read_body_max_chars()
