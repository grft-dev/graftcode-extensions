import { LogsRedactor, commandTypeName, isCommand, runtimeNameOf } from 'hypertube-nodejs-sdk/abstractions'

const TRUNCATION_MARKER = '"__truncated__"'
const MAX_DEPTH = 24
const CLOSING_COST_PER_LEVEL = 18
const SCALAR_RESERVE = 32

export function renderBody(command, maxLength) {
    if (!isCommand(command)) {
        return undefined
    }

    const writer = { out: '', max: maxLength, truncated: false }
    writeCommand(writer, command, 0)
    return { body: LogsRedactor.redactBody(writer.out), truncated: writer.truncated }
}

function writeCommand(writer, command, depth) {
    writer.out += `{"runtime":"${runtimeNameOf(command.runtimeName)}","type":"${commandTypeName(command.commandType)}","payload":[`

    const budget = Math.max(0, writer.max - (depth + 1) * CLOSING_COST_PER_LEVEL - SCALAR_RESERVE)
    const payload = command.payload
    let complete = true
    let hasContent = false

    for (let i = 0; i < payload.length; i++) {
        if (writer.out.length >= budget) {
            writer.truncated = true
            complete = false
            break
        }
        if (hasContent) {
            writer.out += ','
        }
        hasContent = true

        const followsSensitiveKey =
            i > 0 && typeof payload[i - 1] === 'string' && LogsRedactor.isSensitiveKey(payload[i - 1])
        if (!writeValue(writer, payload[i], depth + 1, budget, followsSensitiveKey)) {
            complete = false
            break
        }
    }

    if (!complete) {
        writer.out += hasContent ? `,${TRUNCATION_MARKER}` : TRUNCATION_MARKER
    }
    writer.out += ']}'
    return complete
}

function writeValue(writer, value, depth, budget, forceRedact) {
    if (value === null || value === undefined) {
        writer.out += 'null'
        return true
    }
    if (depth >= MAX_DEPTH) {
        writer.truncated = true
        writer.out += TRUNCATION_MARKER
        return true
    }
    if (isCommand(value)) {
        return writeCommand(writer, value, depth)
    }
    if (forceRedact) {
        return writeString(writer, LogsRedactor.Placeholder, budget)
    }

    switch (typeof value) {
        case 'string':
            return writeString(writer, LogsRedactor.redactValue(value), budget)
        case 'boolean':
            writer.out += String(value)
            return true
        case 'number':
            writer.out += Number.isFinite(value) ? String(value) : `"${value}"`
            return true
        case 'bigint':
            writer.out += value.toString()
            return true
    }

    if (value instanceof Uint8Array) {
        writer.out += `"[bytes:${value.length}]"`
        return true
    }
    return writeString(writer, LogsRedactor.redactValue(format(value)), budget)
}

function writeString(writer, value, budget) {
    const encoded = JSON.stringify(value)
    if (writer.out.length + encoded.length <= budget) {
        writer.out += encoded
        return true
    }

    writer.truncated = true
    const room = Math.max(0, budget - writer.out.length - 2)
    writer.out += JSON.stringify(value.slice(0, room))
    return false
}

function format(value) {
    try {
        return String(value)
    } catch {
        return `[unrenderable:${Object.prototype.toString.call(value)}]`
    }
}
