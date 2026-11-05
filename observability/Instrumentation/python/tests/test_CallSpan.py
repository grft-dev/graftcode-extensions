import os
import zipfile

import pytest
from opentelemetry import trace
from opentelemetry.trace import StatusCode

from hypertube.utils.CommandType import CommandType
from hypertube.utils.connectionData.InMemoryConnectionData import InMemoryConnectionData
from hypertube.utils.logging.LogsRedactor import LogsRedactor

from graftcode_instrumentation import GraftcodeInstrumentation, GraftcodeTelemetrySettings
from graftcode_instrumentation.CallSpan import CallSpan

from Harness import Commands, Harness, NO_HEADERS

TRACE_ID = "0af7651916cd43dd8448eb211c80319c"
PARENT_SPAN_ID = "b7ad6b7169203331"
TRACE_PARENT = "00-" + TRACE_ID + "-" + PARENT_SPAN_ID + "-01"


def single(harness):
    stopped = harness.stopped()
    assert len(stopped) == 1
    return stopped[0]


def trace_parent_of(call_span):
    span_context = call_span.span.get_span_context()
    return "00-{}-{}-{:02x}".format(
        trace.format_trace_id(span_context.trace_id),
        trace.format_span_id(span_context.span_id),
        span_context.trace_flags)


def test_invoke_opens_a_dependency_span_named_after_the_target():
    with Harness() as harness:
        Harness.complete(harness.on_invoke(Commands.fetch(), InMemoryConnectionData()), Commands.value("ok"), None)

        stopped = single(harness)
        assert stopped.instrumentation_scope.name == GraftcodeInstrumentation.INSTRUMENTATION_NAME
        assert stopped.name == "Graft SomeLib inmemory"
        assert stopped.attributes[CallSpan.DEPENDENCY_TYPE] == "Graft"
        assert stopped.attributes[CallSpan.DEPENDENCY_TARGET] == "SomeLib inmemory"
        assert stopped.attributes["hypertube.operation"] == "SomeLib.Service.Fetch"
        assert stopped.attributes["hypertube.command_type"] == "InvokeStaticMethod"
        assert stopped.attributes["hypertube.runtime"] == "python"
        assert stopped.attributes["hypertube.connection_type"] == "InMemory"
        assert stopped.attributes["hypertube.target"] == "inmemory"
        assert stopped.status.status_code == StatusCode.OK
        assert not trace.get_current_span().get_span_context().is_valid


def test_invoke_writes_the_span_into_the_headers_and_restores_them():
    with Harness() as harness:
        connection = InMemoryConnectionData()
        connection.set_thread_local_headers({"tenant": "t1"})

        scope = harness.on_invoke(Commands.fetch(), connection)

        assert connection.get_headers()[CallSpan.TRACE_PARENT_HEADER] == trace_parent_of(scope)
        assert connection.get_headers()[CallSpan.TRACE_ID_HEADER] == trace.format_trace_id(
            scope.span.get_span_context().trace_id)
        assert connection.get_headers()["tenant"] == "t1"

        Harness.complete(scope, None, None)

        assert CallSpan.TRACE_PARENT_HEADER not in connection.get_headers()
        assert CallSpan.TRACE_ID_HEADER not in connection.get_headers()
        assert connection.get_headers()["tenant"] == "t1"


def test_invoke_does_not_replace_a_trace_the_caller_already_set():
    with Harness() as harness:
        connection = InMemoryConnectionData()
        connection.set_headers({CallSpan.TRACE_PARENT_HEADER: TRACE_PARENT})

        scope = harness.on_invoke(Commands.fetch(), connection)

        assert trace.format_trace_id(scope.span.get_span_context().trace_id) == TRACE_ID
        assert connection.get_headers()[CallSpan.TRACE_PARENT_HEADER] == TRACE_PARENT
        Harness.complete(scope, None, None)
        assert trace.format_span_id(single(harness).parent.span_id) == PARENT_SPAN_ID


def test_nested_invoke_on_the_same_connection_propagates_the_inner_span_and_restores_the_outer():
    with Harness() as harness:
        connection = InMemoryConnectionData()

        outer = harness.on_invoke(Commands.fetch(), connection)
        inner = harness.on_invoke(Commands.fetch(), connection)

        assert connection.get_headers()[CallSpan.TRACE_PARENT_HEADER] == trace_parent_of(inner)

        Harness.complete(inner, None, None)
        assert connection.get_headers()[CallSpan.TRACE_PARENT_HEADER] == trace_parent_of(outer)
        assert trace.get_current_span().get_span_context() == outer.span.get_span_context()

        Harness.complete(outer, None, None)
        assert CallSpan.TRACE_PARENT_HEADER not in connection.get_headers()
        assert not trace.get_current_span().get_span_context().is_valid
        assert harness.stopped()[0].parent.span_id == outer.span.get_span_context().span_id


def test_with_auto_inject_disabled_headers_are_left_alone():
    with Harness() as harness:
        GraftcodeTelemetrySettings.set_auto_inject_enabled(False)
        connection = InMemoryConnectionData()

        scope = harness.on_invoke(Commands.fetch(), connection)

        assert CallSpan.TRACE_PARENT_HEADER not in connection.get_headers()
        Harness.complete(scope, None, None)


def test_failed_call_stops_the_span_with_a_redacted_message():
    with Harness() as harness:
        Harness.complete(
            harness.on_invoke(Commands.fetch(), InMemoryConnectionData()),
            None,
            RuntimeError("Password=hunter2"))

        stopped = single(harness)
        assert stopped.status.status_code == StatusCode.ERROR
        assert stopped.status.description == LogsRedactor.PLACEHOLDER
        assert stopped.attributes["exception.type"] == "RuntimeError"
        assert stopped.attributes["exception.message"] == LogsRedactor.PLACEHOLDER


@pytest.mark.parametrize("command_type", [
    CommandType.InvokeDelegate,
    CommandType.PassByValue,
    CommandType.RegisterForUpdate,
    CommandType.ValueForUpdate,
])
def test_transport_commands_do_not_open_a_span(command_type):
    with Harness() as harness:
        assert harness.on_invoke(Commands.of(command_type, "x"), InMemoryConnectionData()) is None
        assert harness.on_handle(Commands.of(command_type, "x"), NO_HEADERS) is None
        assert harness.stopped() == []


def test_telemetry_setup_calls_do_not_open_a_span():
    with Harness() as harness:
        assert harness.on_handle(
            Commands.of(
                CommandType.InvokeStaticMethod,
                Commands.of(CommandType.GetType, Commands.value("app.TelemetrySetup")),
                Commands.value("initialize")),
            NO_HEADERS) is None


def test_unrecorded_span_gets_no_attributes_but_still_propagates():
    with Harness(False, None) as harness:
        connection = InMemoryConnectionData()

        scope = harness.on_invoke(Commands.fetch(), connection)

        assert not scope.span.is_recording()
        assert connection.get_headers()[CallSpan.TRACE_PARENT_HEADER] == trace_parent_of(scope)
        Harness.complete(scope, None, None)
        assert harness.stopped() == []


def test_handle_is_named_after_the_operation_and_parented_on_the_incoming_trace_parent():
    with Harness() as harness:
        scope = harness.on_handle(Commands.fetch(), {CallSpan.TRACE_PARENT_HEADER: TRACE_PARENT})
        Harness.complete(scope, None, None)

        stopped = single(harness)
        assert stopped.name == "SomeLib.Service.Fetch"
        assert trace.format_trace_id(stopped.context.trace_id) == TRACE_ID
        assert trace.format_span_id(stopped.parent.span_id) == PARENT_SPAN_ID


def test_handle_falls_back_to_the_bare_trace_id():
    with Harness() as harness:
        scope = harness.on_handle(Commands.fetch(), {CallSpan.TRACE_ID_HEADER: TRACE_ID})
        Harness.complete(scope, None, None)

        assert trace.format_trace_id(single(harness).context.trace_id) == TRACE_ID


def test_loaded_library_names_the_target_of_later_calls(tmp_path):
    jar = os.path.join(str(tmp_path), "TestGgJavaLib.jar")
    with zipfile.ZipFile(jar, "w") as archive:
        archive.writestr("META-INF/MANIFEST.MF", "Manifest-Version: 1.0\nImplementation-Version: 1.2.3\n")

    with Harness() as harness:
        connection = InMemoryConnectionData()

        Harness.complete(harness.on_invoke(Commands.of(CommandType.LoadLibrary, Commands.value(jar)), connection),
                         None, None)
        Harness.complete(harness.on_invoke(
            Commands.of(
                CommandType.InvokeStaticMethod,
                Commands.of(CommandType.GetType, Commands.value("testggjavalib.RandomService")),
                Commands.value("fetch")),
            connection), None, None)

        assert harness.stopped()[1].name == "Graft TestGgJavaLib 1.2.3 inmemory"


def test_loaded_python_package_names_the_target_without_a_version(tmp_path):
    package = tmp_path / "TestGgPythonLib"
    package.mkdir()

    with Harness() as harness:
        connection = InMemoryConnectionData()

        Harness.complete(harness.on_invoke(Commands.of(CommandType.LoadLibrary, Commands.value(str(package))),
                                           connection), None, None)
        Harness.complete(harness.on_invoke(
            Commands.of(
                CommandType.InvokeStaticMethod,
                Commands.of(CommandType.GetType, Commands.value("TestGgPythonLib.RandomService.RandomService")),
                Commands.value("fetch")),
            connection), None, None)

        assert harness.stopped()[1].name == "Graft TestGgPythonLib inmemory"
