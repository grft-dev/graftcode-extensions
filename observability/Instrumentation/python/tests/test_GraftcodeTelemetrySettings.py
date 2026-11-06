from graftcode_instrumentation import GraftcodeTelemetrySettings, INSTRUMENTATION_NAME, LOGGER_NAME


def test_names_match_the_dotnet_and_java_instrumentation() -> None:
    assert INSTRUMENTATION_NAME == "Graftcode"
    assert LOGGER_NAME == "Hypertube"


def test_auto_inject_is_enabled_by_default() -> None:
    assert GraftcodeTelemetrySettings.is_auto_inject_enabled()


def test_body_max_characters_defaults_to_8192() -> None:
    assert GraftcodeTelemetrySettings.get_body_max_characters() == GraftcodeTelemetrySettings.DEFAULT_BODY_MAX_CHARS
