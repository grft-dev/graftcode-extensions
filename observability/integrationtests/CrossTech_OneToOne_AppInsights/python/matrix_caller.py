from __future__ import annotations

import json
import os
import sys
from pathlib import Path

_ROOT = Path(__file__).resolve().parents[1]
_VENV = Path(r"C:\dev\TestGgPythonWebApi\.venv\Lib\site-packages")
_HT = Path(r"C:\dev\NewHT\HYPERTUBE\src\python")
for p in (_VENV, _HT):
    if p.exists():
        sys.path.insert(0, str(p))

from graftcode_telemetry_appinsights import GraftCodeAppInsightsConnector
from hypertube.sdk import RuntimeBridge
from hypertube.utils.connectionData.WsConnectionData import WsConnectionData
from opentelemetry import trace
from opentelemetry.trace import SpanKind


def runtime(factory, name: str):
    return getattr(factory, name)()


def main() -> int:
    config_path = Path(sys.argv[1] if len(sys.argv) > 1 else _ROOT / "matrix-config.json")
    hosts = json.loads(config_path.read_text(encoding="utf-8"))["hosts"]
    connection_string = os.environ.get("APPLICATIONINSIGHTS_CONNECTION_STRING")
    if not connection_string:
        print("APPLICATIONINSIGHTS_CONNECTION_STRING is not set", file=sys.stderr)
        return 2

    def configure(options):
        options.service_name = "CrossTech.OneToOne.PythonCaller"
        options.connection_string = connection_string

    with GraftCodeAppInsightsConnector.start(configure) as telemetry:
        for callee_key, callee in hosts.items():
            input_value = f"matrix-python-{callee_key.lower()}"
            span_name = f"CrossMatrix.Python->{callee_key}"
            try:
                with telemetry.tracer.start_as_current_span(span_name, kind=SpanKind.SERVER):
                    span_ctx = trace.get_current_span().get_span_context()
                    trace_id = trace.format_trace_id(span_ctx.trace_id) if span_ctx.is_valid else ""
                    telemetry.logger.info(
                        "CrossMatrix Caller=Python Callee={Callee} Input={Input} TraceId={TraceId}",
                        callee_key,
                        input_value,
                        trace_id,
                    )
                    factory = RuntimeBridge.web_socket(WsConnectionData(callee["ws"]))
                    rt = runtime(factory, callee["runtimeArg"])
                    result = str(
                        rt.get_type(callee["typeName"])
                        .invoke_static_method(callee["methodName"], input_value)
                        .execute()
                        .get_value()
                    )
                    telemetry.logger.info("CrossMatrix after call Result={Result}", result)
                telemetry.force_flush(15000)
                print(f"OK|Python|{callee_key}|{trace_id}|{result.replace('|', '/')}")
            except Exception as ex:
                print(f"FAIL|Python|{callee_key}||{type(ex).__name__}: {str(ex).replace('|', '/')}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())