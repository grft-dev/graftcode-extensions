import { CommandType, isCommand } from 'hypertube-nodejs-sdk/abstractions'

const MAX_DEPTH = 32

export function isBlank(value) {
    return value == null || String(value).trim() === ''
}

export function resolve(command) {
    if (!isCommand(command)) {
        return undefined
    }

    switch (command.commandType) {
        case CommandType.InvokeStaticMethod:
        case CommandType.InvokeInstanceMethod:
        case CommandType.InvokeGenericStaticMethod:
        case CommandType.InvokeGenericMethod:
        case CommandType.GetStaticField:
        case CommandType.SetStaticField:
        case CommandType.GetInstanceField:
        case CommandType.SetInstanceField:
        case CommandType.GetStaticMethodAsDelegate:
        case CommandType.GetInstanceMethodAsDelegate: {
            const qualified = qualify(
                resolveTypeNameCore(command.payload[0], 0),
                resolveText(command.payload[1], 0)
            )
            return qualified ?? fallback(command)
        }

        case CommandType.CreateClassInstance: {
            const createdType = resolveTypeNameCore(command.payload[0], 0)
            return createdType == null ? fallback(command) : `${createdType}.ctor`
        }

        case CommandType.InvokeGlobalFunction:
        case CommandType.GetGlobalField: {
            const name = resolveText(command.payload[0], 0)
            return name ?? fallback(command)
        }

        default:
            return fallback(command)
    }
}

export function resolveTargetTypeName(command) {
    if (!isCommand(command)) {
        return undefined
    }

    switch (command.commandType) {
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
            return resolveTypeNameCore(command.payload[0], 0)

        case CommandType.GetType:
            return resolveTypeNameCore(command, 0)

        default:
            return undefined
    }
}

function fallback(command) {
    return `hypertube.${command.commandType}`
}

function qualify(typeName, memberName) {
    if (isBlank(memberName)) {
        return undefined
    }
    return isBlank(typeName) ? memberName : `${typeName}.${memberName}`
}

function resolveText(payloadItem, depth) {
    if (depth > MAX_DEPTH) {
        return undefined
    }
    if (typeof payloadItem === 'string') {
        return payloadItem.trim() || undefined
    }
    if (isCommand(payloadItem) && payloadItem.commandType === CommandType.Value) {
        return resolveText(payloadItem.payload[0], depth + 1)
    }
    return undefined
}

function resolveTypeNameCore(target, depth) {
    if (depth > MAX_DEPTH || target == null) {
        return undefined
    }
    if (typeof target === 'string') {
        return target.trim() || undefined
    }
    if (!isCommand(target)) {
        return undefined
    }
    return resolveTypeNameFromCommand(target, depth)
}

function resolveTypeNameFromCommand(command, depth) {
    switch (command.commandType) {
        case CommandType.Value:
            return resolveTypeNameCore(command.payload[0], depth + 1)

        case CommandType.GetType:
        case CommandType.Cast:
        case CommandType.CreateNull:
            for (const item of command.payload) {
                const name = resolveText(item, depth + 1)
                if (!isBlank(name)) {
                    return name
                }
            }
            return undefined

        case CommandType.CreateClassInstance:
        case CommandType.SetInstanceField:
        case CommandType.InvokeStaticMethod:
        case CommandType.InvokeInstanceMethod:
        case CommandType.GetStaticField:
        case CommandType.GetInstanceField:
            return resolveTypeNameCore(command.payload[0], depth + 1)

        default:
            return undefined
    }
}
