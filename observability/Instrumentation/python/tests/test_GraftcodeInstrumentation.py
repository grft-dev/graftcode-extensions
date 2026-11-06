import logging

from hypertube.core.interpreter.Interpreter import Interpreter
from hypertube.utils.Command import Command
from hypertube.utils.CommandType import CommandType
from hypertube.utils.RuntimeName import RuntimeName
from hypertube.utils.connectionData.InMemoryConnectionData import InMemoryConnectionData
from hypertube.utils.logging.HypertubeLogger import HypertubeLogger
from hypertube.utils.logging.LogsLevel import LogsLevel
from hypertube.utils.logging.LogsRedactor import LogsRedactor

from Harness import Commands, Harness, NO_HEADERS


def test_attach_hooks_into_hypertube_and_close_detaches():
    HypertubeLogger.minimum_level = LogsLevel.Error
    harness = Harness(True, LogsLevel.Info)
    assert HypertubeLogger.is_enabled(LogsLevel.Info)

    harness.close()
    assert not HypertubeLogger.is_enabled(LogsLevel.Info)


def test_without_a_logger_logs_become_span_events_with_the_exception_redacted():
    with Harness() as harness:
        scope = harness.on_handle(Commands.fetch(), NO_HEADERS)
        assert harness.listener.is_log_enabled(LogsLevel.Info)
        harness.listener.on_log(
            LogsLevel.Info,
            "Hypertube handling InvokeStaticMethod",
            {"CommandType": "InvokeStaticMethod"},
            None)
        harness.listener.on_log(LogsLevel.Error, "failed", NO_HEADERS, RuntimeError("Password=hunter2"))
        Harness.complete(scope, None, None)

        events = harness.stopped()[0].events
        assert events[0].name == "Hypertube handling InvokeStaticMethod"
        assert events[0].attributes["CommandType"] == "InvokeStaticMethod"
        assert events[0].attributes["log.level"] == "Information"
        assert events[1].name == "exception"
        assert events[1].attributes["exception.message"] == LogsRedactor.PLACEHOLDER
        for value in events[1].attributes.values():
            assert "hunter2" not in str(value)


def test_with_a_logger_logs_are_forwarded_instead_of_recorded_on_the_span():
    with Harness(True, LogsLevel.Trace) as harness:
        scope = harness.on_handle(Commands.fetch(), NO_HEADERS)
        harness.listener.on_log(LogsLevel.Warning, "hello ada", {"Name": "ada"}, None)
        Harness.complete(scope, None, None)

        assert len(harness.logs()) == 1
        entry = harness.logs()[0]
        assert entry.level == logging.WARNING
        assert entry.body == "hello ada"
        assert entry.span_id == scope.span.get_span_context().span_id
        assert entry.record.Name == "ada"
        assert len(harness.stopped()[0].events) == 0


def test_logger_at_information_receives_hypertube_lines_at_its_level_only():
    with Harness(True, LogsLevel.Info) as harness:
        HypertubeLogger.get_logger().log(LogsLevel.Debug, "Hypertube debug {Detail}", "x")
        HypertubeLogger.get_logger().log(LogsLevel.Info, "Hypertube invoking {CommandType}", "InvokeStaticMethod")

        assert len(harness.logs()) == 1
        assert harness.logs()[0].level == logging.INFO
        assert harness.logs()[0].body == "Hypertube invoking InvokeStaticMethod"


def test_logger_level_decides_what_is_enabled():
    with Harness(True, LogsLevel.Error) as harness:
        assert not harness.listener.is_log_enabled(LogsLevel.Warning)
        assert harness.listener.is_log_enabled(LogsLevel.Error)


def test_logger_off_disables_forwarding():
    HypertubeLogger.minimum_level = LogsLevel.Error
    with Harness(True, LogsLevel.Off) as harness:
        assert not HypertubeLogger.is_enabled(LogsLevel.Info)
        assert not harness.listener.is_log_enabled(LogsLevel.Error)


def test_without_an_exporter_logs_are_disabled():
    with Harness(False, None) as harness:
        scope = harness.on_handle(Commands.fetch(), NO_HEADERS)

        assert not harness.listener.is_log_enabled(LogsLevel.Error)
        Harness.complete(scope, None, None)


def test_reserved_record_names_do_not_break_forwarding():
    with Harness(True, LogsLevel.Info) as harness:
        harness.listener.on_log(LogsLevel.Info, "hello", {"message": "m", "name": "n"}, None)

        assert harness.logs()[0].record.message_ == "m"
        assert harness.logs()[0].record.name_ == "n"


def test_in_memory_call_produces_an_invoke_span_and_a_handle_span_in_the_same_trace():
    with Harness(True, LogsLevel.Info) as harness:
        response = Interpreter.execute(
            Command(RuntimeName.python, CommandType.InvokeStaticMethod,
                    [Command(RuntimeName.python, CommandType.GetType, ["math"]), "sqrt", 16.0]),
            InMemoryConnectionData())

        assert response.command_type == CommandType.Value
        spans = harness.stopped()
        assert len(spans) == 2
        handle, invoke = spans
        assert handle.name == "math.sqrt"
        assert invoke.name == "Graft math inmemory"
        assert invoke.context.trace_id == handle.context.trace_id
        assert invoke.context.span_id == handle.parent.span_id

        logs = harness.logs()
        assert len(logs) == 4
        assert logs[0].body == "Hypertube invoking InvokeStaticMethod on the python runtime"
        assert logs[0].span_id == invoke.context.span_id
        assert logs[1].span_id == handle.context.span_id
