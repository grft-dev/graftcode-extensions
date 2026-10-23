using System;
using Hypertube.Netcore.Utils;

namespace Graftcode.Instrumentation
{
    internal static class OperationName
    {
        private const int MaxDepth = 32;

        internal static string Resolve(ICommand command)
        {
            switch (command.CommandType)
            {
                case CommandType.InvokeStaticMethod:
                case CommandType.InvokeInstanceMethod:
                case CommandType.InvokeGenericStaticMethod:
                case CommandType.InvokeGenericMethod:
                case CommandType.GetStaticField:
                case CommandType.SetStaticField:
                case CommandType.GetInstanceField:
                case CommandType.SetInstanceField:
                case CommandType.GetStaticMethodAsDelegate:
                case CommandType.GetInstanceMethodAsDelegate:
                    return Qualify(
                        ResolveTypeNameCore(PayloadAt(command, 0), 0),
                        ResolveText(PayloadAt(command, 1), 0))
                        ?? Fallback(command);

                case CommandType.CreateClassInstance:
                    var createdType = ResolveTypeNameCore(PayloadAt(command, 0), 0);
                    return createdType == null ? Fallback(command) : createdType + ".ctor";

                case CommandType.InvokeGlobalFunction:
                case CommandType.GetGlobalField:
                    return ResolveText(PayloadAt(command, 0), 0) ?? Fallback(command);

                default:
                    return Fallback(command);
            }
        }

        internal static string? ResolveTargetTypeName(ICommand command)
        {
            switch (command.CommandType)
            {
                case CommandType.InvokeStaticMethod:
                case CommandType.InvokeInstanceMethod:
                case CommandType.InvokeGenericStaticMethod:
                case CommandType.InvokeGenericMethod:
                case CommandType.GetStaticField:
                case CommandType.SetStaticField:
                case CommandType.GetInstanceField:
                case CommandType.SetInstanceField:
                case CommandType.GetStaticMethodAsDelegate:
                case CommandType.GetInstanceMethodAsDelegate:
                case CommandType.CreateClassInstance:
                    return ResolveTypeNameCore(PayloadAt(command, 0), 0);

                case CommandType.GetType:
                    return ResolveTypeNameCore(command, 0);

                default:
                    return null;
            }
        }

        private static string Fallback(ICommand command) => $"hypertube.{command.CommandType}";

        private static string? Qualify(string? typeName, string? memberName)
        {
            if (string.IsNullOrWhiteSpace(memberName))
            {
                return null;
            }

            return string.IsNullOrWhiteSpace(typeName) ? memberName : $"{typeName}.{memberName}";
        }

        private static object? PayloadAt(ICommand command, int index)
        {
            return command.Payload.Length > index ? command.Payload[index] : null;
        }

        private static string? ResolveText(object? payloadItem, int depth)
        {
            if (depth > MaxDepth)
            {
                return null;
            }

            return payloadItem switch
            {
                string text => text,
                ICommand { CommandType: CommandType.Value } command => ResolveText(PayloadAt(command, 0), depth + 1),
                _ => null,
            };
        }

        private static string? ResolveTypeNameCore(object? target, int depth)
        {
            if (depth > MaxDepth || target == null)
            {
                return null;
            }

            return target switch
            {
                string typeName => typeName,
                Type type => type.FullName ?? type.Name,
                ICommand command => ResolveTypeNameFromCommand(command, depth),
                _ => target.GetType().FullName,
            };
        }

        private static string? ResolveTypeNameFromCommand(ICommand command, int depth)
        {
            switch (command.CommandType)
            {
                case CommandType.Value:
                    return ResolveTypeNameCore(PayloadAt(command, 0), depth + 1);

                case CommandType.GetType:
                case CommandType.Cast:
                case CommandType.CreateNull:
                    for (int i = 0; i < command.Payload.Length; i++)
                    {
                        var name = ResolveText(command.Payload[i], depth + 1);
                        if (!string.IsNullOrWhiteSpace(name))
                        {
                            return name;
                        }
                    }

                    return null;

                case CommandType.CreateClassInstance:
                case CommandType.SetInstanceField:
                case CommandType.InvokeStaticMethod:
                case CommandType.InvokeInstanceMethod:
                case CommandType.GetStaticField:
                case CommandType.GetInstanceField:
                    return ResolveTypeNameCore(PayloadAt(command, 0), depth + 1);

                default:
                    return null;
            }
        }
    }
}
