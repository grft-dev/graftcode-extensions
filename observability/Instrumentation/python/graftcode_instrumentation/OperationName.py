from hypertube.utils.Command import Command
from hypertube.utils.CommandType import CommandType

_MAX_DEPTH = 32

_MEMBER_COMMANDS = (
    CommandType.InvokeStaticMethod,
    CommandType.InvokeInstanceMethod,
    CommandType.InvokeGenericStaticMethod,
    CommandType.InvokeGenericMethod,
    CommandType.GetStaticField,
    CommandType.SetStaticField,
    CommandType.GetInstanceField,
    CommandType.SetInstanceField,
    CommandType.GetStaticMethodAsDelegate,
    CommandType.GetInstanceMethodAsDelegate,
)

_NAMED_BY_PAYLOAD = (
    CommandType.GetType,
    CommandType.Cast,
    CommandType.CreateNull,
)

_TYPED_BY_FIRST_PAYLOAD = (
    CommandType.CreateClassInstance,
    CommandType.SetInstanceField,
    CommandType.InvokeStaticMethod,
    CommandType.InvokeInstanceMethod,
    CommandType.GetStaticField,
    CommandType.GetInstanceField,
)


class OperationName:

    @staticmethod
    def resolve(command):
        command_type = command.command_type

        if command_type in _MEMBER_COMMANDS:
            qualified = OperationName._qualify(
                OperationName._resolve_type_name_core(OperationName._payload_at(command, 0), 0),
                OperationName._resolve_text(OperationName._payload_at(command, 1), 0))
            return qualified if qualified is not None else OperationName._fallback(command)

        if command_type == CommandType.CreateClassInstance:
            created_type = OperationName._resolve_type_name_core(OperationName._payload_at(command, 0), 0)
            return OperationName._fallback(command) if created_type is None else created_type + ".ctor"

        if command_type in (CommandType.InvokeGlobalFunction, CommandType.GetGlobalField):
            name = OperationName._resolve_text(OperationName._payload_at(command, 0), 0)
            return name if name is not None else OperationName._fallback(command)

        return OperationName._fallback(command)

    @staticmethod
    def resolve_target_type_name(command):
        command_type = command.command_type

        if command_type in _MEMBER_COMMANDS or command_type == CommandType.CreateClassInstance:
            return OperationName._resolve_type_name_core(OperationName._payload_at(command, 0), 0)

        if command_type == CommandType.GetType:
            return OperationName._resolve_type_name_core(command, 0)

        return None

    @staticmethod
    def is_blank(value):
        return value is None or not str(value).strip()

    @staticmethod
    def _fallback(command):
        return "hypertube." + command.command_type.name

    @staticmethod
    def _qualify(type_name, member_name):
        if OperationName.is_blank(member_name):
            return None

        return member_name if OperationName.is_blank(type_name) else type_name + "." + member_name

    @staticmethod
    def _payload_at(command, index):
        payload = command.payload
        return payload[index] if len(payload) > index else None

    @staticmethod
    def _resolve_text(payload_item, depth):
        if depth > _MAX_DEPTH:
            return None

        if isinstance(payload_item, str):
            return payload_item

        if isinstance(payload_item, Command) and payload_item.command_type == CommandType.Value:
            return OperationName._resolve_text(OperationName._payload_at(payload_item, 0), depth + 1)

        return None

    @staticmethod
    def _resolve_type_name_core(target, depth):
        if depth > _MAX_DEPTH or target is None:
            return None

        if isinstance(target, str):
            return target

        if isinstance(target, type):
            return OperationName._full_name(target)

        if isinstance(target, Command):
            return OperationName._resolve_type_name_from_command(target, depth)

        return OperationName._full_name(type(target))

    @staticmethod
    def _resolve_type_name_from_command(command, depth):
        command_type = command.command_type

        if command_type == CommandType.Value:
            return OperationName._resolve_type_name_core(OperationName._payload_at(command, 0), depth + 1)

        if command_type in _NAMED_BY_PAYLOAD:
            for item in command.payload:
                name = OperationName._resolve_text(item, depth + 1)
                if not OperationName.is_blank(name):
                    return name
            return None

        if command_type in _TYPED_BY_FIRST_PAYLOAD:
            return OperationName._resolve_type_name_core(OperationName._payload_at(command, 0), depth + 1)

        return None

    @staticmethod
    def _full_name(value_type):
        module = value_type.__module__
        if module in (None, "builtins"):
            return value_type.__qualname__
        return module + "." + value_type.__qualname__
