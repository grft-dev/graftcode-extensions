from hypertube.utils.Command import Command
from hypertube.utils.logging.LogsRedactor import LogsRedactor

from graftcode_instrumentation.OperationName import OperationName


class BodyCaptureResult:
    __slots__ = ("body", "truncated")

    def __init__(self, body, truncated):
        self.body = body
        self.truncated = truncated


class BodyCapture:
    TRUNCATION_MARKER = '"__truncated__"'

    _MAX_DEPTH = 24
    _CLOSING_COST_PER_LEVEL = 18
    _SCALAR_RESERVE = 32

    @staticmethod
    def render(command, max_characters):
        if command is None:
            return None

        truncated = [False]
        builder = _Builder()
        BodyCapture._write_command(builder, command, 0, max_characters, truncated)

        return BodyCaptureResult(LogsRedactor.redact_body(builder.to_string()), truncated[0])

    @staticmethod
    def _write_command(builder, command, depth, budget, truncated):
        builder.append('{"runtime":"')
        builder.append(str(command.runtime_name))
        builder.append('","type":"')
        builder.append(command.command_type.name)
        builder.append('","payload":[')

        payload = command.payload
        within_budget = True
        has_content = False
        local_budget = max(0, budget - ((depth + 1) * BodyCapture._CLOSING_COST_PER_LEVEL) - BodyCapture._SCALAR_RESERVE)

        for i, item in enumerate(payload):
            if builder.length >= local_budget:
                truncated[0] = True
                within_budget = False
                break

            if has_content:
                builder.append(",")

            follows_sensitive_key = (i > 0
                                     and isinstance(payload[i - 1], str)
                                     and LogsRedactor.is_sensitive_key(payload[i - 1]))

            if not BodyCapture._write_value(builder, item, depth + 1, budget, local_budget,
                                            follows_sensitive_key, truncated):
                has_content = True
                within_budget = False
                break

            has_content = True

        if not within_budget:
            if has_content:
                builder.append(",")

            builder.append(BodyCapture.TRUNCATION_MARKER)

        builder.append("]}")
        return within_budget

    @staticmethod
    def _write_value(builder, value, depth, budget, local_budget, force_redact, truncated):
        if value is None:
            builder.append("null")
            return True

        if depth >= BodyCapture._MAX_DEPTH:
            truncated[0] = True
            builder.append(BodyCapture.TRUNCATION_MARKER)
            return True

        if isinstance(value, Command):
            return BodyCapture._write_command(builder, value, depth, budget, truncated)

        if isinstance(value, str):
            return BodyCapture._write_string(
                builder,
                LogsRedactor.PLACEHOLDER if force_redact else LogsRedactor.redact_value(value),
                local_budget,
                truncated)

        if isinstance(value, bool):
            builder.append("true" if value else "false")
            return True

        if isinstance(value, (int, float)):
            builder.append(repr(value))
            return True

        if isinstance(value, (bytes, bytearray)):
            builder.append('"[bytes:' + str(len(value)) + ']"')
            return True

        return BodyCapture._write_string(
            builder,
            LogsRedactor.PLACEHOLDER if force_redact else LogsRedactor.redact_value(BodyCapture._format(value)),
            local_budget,
            truncated)

    @staticmethod
    def _format(value):
        try:
            return str(value)
        except Exception:
            return "[unrenderable:" + OperationName._full_name(type(value)) + "]"

    @staticmethod
    def _write_string(builder, value, budget, truncated):
        builder.append('"')

        if value is None:
            builder.append('"')
            return True

        for character in value:
            if builder.length >= budget:
                truncated[0] = True
                builder.append('"')
                return False

            if character == '"':
                builder.append('\\"')
            elif character == "\\":
                builder.append("\\\\")
            elif character == "\n":
                builder.append("\\n")
            elif character == "\r":
                builder.append("\\r")
            elif character == "\t":
                builder.append("\\t")
            elif character < " ":
                builder.append("\\u%04x" % ord(character))
            else:
                builder.append(character)

        builder.append('"')
        return True


class _Builder:
    __slots__ = ("_parts", "length")

    def __init__(self):
        self._parts = []
        self.length = 0

    def append(self, text):
        self._parts.append(text)
        self.length += len(text)

    def to_string(self):
        return "".join(self._parts)
