import { CommandType, LogsRedactor, commandTypeName, isCommand } from 'hypertube-nodejs-sdk/abstractions'

const MAX_DEPTH = 32

const MEMBER_COMMANDS = new Set([
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
])

const TYPE_FROM_TARGET_COMMANDS = new Set([
    CommandType.CreateClassInstance,
    CommandType.SetInstanceField,
    CommandType.InvokeStaticMethod,
    CommandType.InvokeInstanceMethod,
    CommandType.GetStaticField,
    CommandType.GetInstanceField,
])

export function resolveOperation(command) {
    if (!isCommand(command)) {
        return undefined
    }

    let service
    let method

    if (MEMBER_COMMANDS.has(command.commandType)) {
        service = resolveTypeName(command.payload[0], 0)
        method = resolveText(command.payload[1], 0)
    } else if (command.commandType === CommandType.CreateClassInstance) {
        service = resolveTypeName(command.payload[0], 0)
        method = service ? 'constructor' : undefined
    } else if (
        command.commandType === CommandType.InvokeGlobalFunction ||
        command.commandType === CommandType.GetGlobalField
    ) {
        method = resolveText(command.payload[0], 0)
    }

    if (!method) {
        return undefined
    }

    service = service ? LogsRedactor.redactValue(service) : undefined
    method = LogsRedactor.redactValue(method)
    return {
        name: service ? `${service}.${method}` : method,
        service,
        method,
        commandType: commandTypeName(command.commandType),
    }
}

function resolveText(item, depth) {
    if (depth > MAX_DEPTH) {
        return undefined
    }
    if (typeof item === 'string') {
        return item.trim() || undefined
    }
    if (isCommand(item) && item.commandType === CommandType.Value) {
        return resolveText(item.payload[0], depth + 1)
    }
    return undefined
}

function resolveTypeName(target, depth) {
    if (depth > MAX_DEPTH || target == null) {
        return undefined
    }
    if (typeof target === 'string') {
        return target.trim() || undefined
    }
    if (!isCommand(target)) {
        return undefined
    }

    switch (target.commandType) {
        case CommandType.Value:
            return resolveTypeName(target.payload[0], depth + 1)
        case CommandType.GetType:
        case CommandType.Cast:
        case CommandType.CreateNull:
            for (const item of target.payload) {
                const name = resolveText(item, depth + 1)
                if (name) {
                    return name
                }
            }
            return undefined
        default:
            return TYPE_FROM_TARGET_COMMANDS.has(target.commandType)
                ? resolveTypeName(target.payload[0], depth + 1)
                : undefined
    }
}
