import math

from hypertube.utils.CommandType import CommandType

from graftcode_instrumentation.OperationName import OperationName

from Harness import Commands


def test_static_method_is_qualified_with_its_type() -> None:
    assert OperationName.resolve(Commands.fetch()) == "SomeLib.Service.Fetch"


def test_instance_method_on_a_created_instance_uses_the_created_type() -> None:
    assert OperationName.resolve(Commands.of(
        CommandType.InvokeInstanceMethod,
        Commands.of(CommandType.CreateClassInstance, Commands.of(CommandType.GetType, "app.Basket")),
        "add")) == "app.Basket.add"


def test_constructor_is_named_ctor() -> None:
    assert OperationName.resolve(Commands.of(
        CommandType.CreateClassInstance,
        Commands.of(CommandType.GetType, "app.Basket"))) == "app.Basket.ctor"


def test_class_payload_uses_its_name() -> None:
    class Basket:
        pass

    assert OperationName.resolve(Commands.of(CommandType.InvokeStaticMethod, Basket, "add")) == (
        Basket.__module__ + "." + Basket.__qualname__ + ".add")


def test_module_payload_uses_its_type_name() -> None:
    assert OperationName.resolve(Commands.of(CommandType.InvokeStaticMethod, math, "sqrt")) == "module.sqrt"


def test_global_function_uses_its_name() -> None:
    assert OperationName.resolve(Commands.of(CommandType.InvokeGlobalFunction, "module.run")) == "module.run"


def test_unknown_shape_falls_back_to_the_command_type() -> None:
    assert OperationName.resolve(Commands.of(CommandType.LoadLibrary, "x.py")) == "hypertube.LoadLibrary"
    assert OperationName.resolve(Commands.of(CommandType.InvokeStaticMethod, "type")) == "hypertube.InvokeStaticMethod"
