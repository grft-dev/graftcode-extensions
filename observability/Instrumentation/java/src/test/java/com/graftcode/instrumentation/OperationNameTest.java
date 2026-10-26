package com.graftcode.instrumentation;

import com.graftcode.instrumentation.Harness.Commands;
import com.hypertube.utils.CommandType;
import org.junit.jupiter.api.Assertions;
import org.junit.jupiter.api.Test;

class OperationNameTest {

    @Test
    void staticMethod_IsQualifiedWithItsType() {
        Assertions.assertEquals("SomeLib.Service.Fetch", OperationName.resolve(Commands.fetch()));
    }

    @Test
    void instanceMethodOnACreatedInstance_UsesTheCreatedType() {
        Assertions.assertEquals("app.Basket.add", OperationName.resolve(Commands.of(
                CommandType.INVOKE_INSTANCE_METHOD,
                Commands.of(CommandType.CREATE_INSTANCE, Commands.of(CommandType.GET_TYPE, "app.Basket")),
                "add")));
    }

    @Test
    void constructor_IsNamedCtor() {
        Assertions.assertEquals("app.Basket.ctor", OperationName.resolve(Commands.of(
                CommandType.CREATE_INSTANCE,
                Commands.of(CommandType.GET_TYPE, "app.Basket"))));
    }

    @Test
    void classPayload_UsesItsName() {
        Assertions.assertEquals("java.lang.Math.abs", OperationName.resolve(Commands.of(
                CommandType.INVOKE_STATIC_METHOD,
                Math.class,
                "abs")));
    }

    @Test
    void globalFunction_UsesItsName() {
        Assertions.assertEquals("module.run", OperationName.resolve(Commands.of(CommandType.INVOKE_GLOBAL_FUNCTION, "module.run")));
    }

    @Test
    void unknownShape_FallsBackToTheCommandType() {
        Assertions.assertEquals("hypertube.LOAD_LIBRARY", OperationName.resolve(Commands.of(CommandType.LOAD_LIBRARY, "x.jar")));
        Assertions.assertEquals("hypertube.INVOKE_STATIC_METHOD", OperationName.resolve(Commands.of(CommandType.INVOKE_STATIC_METHOD, "type")));
    }
}
