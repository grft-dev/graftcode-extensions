using Hypertube.Netcore.Utils;
using Xunit;

namespace Graftcode.Instrumentation.Tests
{
    public class OperationNameTests
    {
        [Fact]
        [Trait("Test", "Unit")]
        public void OperationName_UsesTypeAndMethodOfTheInvocation()
        {
            var invoke = Commands.Of(
                CommandType.InvokeStaticMethod,
                Commands.Of(CommandType.GetType, Commands.Value("TestGgLib.RandomService")),
                Commands.Value("Fetch"),
                Commands.Value("Pawel"));

            Assert.Equal("TestGgLib.RandomService.Fetch", OperationName.Resolve(invoke));
        }

        [Fact]
        [Trait("Test", "Unit")]
        public void OperationName_ReadsTypeNameFromGetTypeChainedOnLoadLibrary()
        {
            var nested = new Command(
                RuntimeName.Ruby,
                CommandType.InvokeStaticMethod,
                new Command(
                    RuntimeName.Ruby,
                    CommandType.GetType,
                    new Command(RuntimeName.Ruby, CommandType.LoadLibrary, "abc"),
                    "ClassForStaticTest"),
                "three_numbers_adder",
                5,
                10,
                15);

            Assert.Equal("ClassForStaticTest.three_numbers_adder", OperationName.Resolve(nested));
        }

        [Fact]
        [Trait("Test", "Unit")]
        public void OperationName_FallsBackToTheMember_WhenTargetIsAReference()
        {
            var invoke = Commands.Of(
                CommandType.InvokeInstanceMethod,
                Commands.Of(CommandType.Reference, "8b1f0d7c"),
                Commands.Value("Fetch"));

            Assert.Equal("Fetch", OperationName.Resolve(invoke));
        }

        [Fact]
        [Trait("Test", "Unit")]
        public void OperationName_FallsBackToCommandType_ForTransportCommands()
        {
            var loadLibrary = Commands.Of(CommandType.LoadLibrary, Commands.Value("TestGgLib.dll"));

            Assert.Equal("hypertube.LoadLibrary", OperationName.Resolve(loadLibrary));
        }
    }
}
