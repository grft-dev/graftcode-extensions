import { randomBytes } from 'node:crypto'
import { SpanKind, SpanStatusCode, TraceFlags, context, isSpanContextValid, propagation, trace } from '@opentelemetry/api'
import {
    CommandType,
    LogsRedactor,
    commandTypeName,
    connectionTypeName,
    isCommand,
    runtimeNameOf,
} from 'hypertube-nodejs-sdk/abstractions'
import { renderBody } from './bodyCapture.js'
import * as GraftcodeTelemetrySettings from './graftcodeTelemetrySettings.js'
import { describeTarget, record } from './libraryCatalog.js'
import { resolveOperation } from './resolveOperation.js'

export const DEPENDENCY_TYPE = 'graft.dependency.type'
export const DEPENDENCY_TARGET = 'graft.dependency.target'
export const BRIDGE_DEPENDENCY_TYPE = 'Graft'
export const REQUEST_BODY = 'hypertube.request.body'
export const RESPONSE_BODY = 'hypertube.response.body'
export const REQUEST_BODY_TRUNCATED = 'hypertube.request.body.truncated'
export const RESPONSE_BODY_TRUNCATED = 'hypertube.response.body.truncated'
export const TRACE_PARENT_HEADER = 'traceparent'
export const TRACE_STATE_HEADER = 'tracestate'
export const TRACE_ID_HEADER = 'traceid'

const SKIP_COMMAND_TYPES = new Set([
    CommandType.PassByValue,
    CommandType.RegisterForUpdate,
    CommandType.ValueForUpdate,
    CommandType.InvokeDelegate,
])

export class CallSpan {
    #span
    #settings
    #completed = false

    constructor(span, settings) {
        this.#span = span
        this.#settings = settings
    }

    static startInvoke(tracer, callContext, command, connectionData, settings) {
        const operation = tryResolveOperation(command, settings)
        if (!operation) {
            return null
        }

        record(command, connectionData)
        const target = describeTarget(command, connectionData, settings.libraries, operation)
        const parent = context.active()
        const span = tracer.startSpan(
            `Graft ${target}`,
            {
                kind: SpanKind.INTERNAL,
                attributes: invokeAttributes(operation, command, connectionData, target),
            },
            parent
        )
        const callSpan = new CallSpan(span, settings)
        callSpan.#pushScope(callContext, parent, span)
        callSpan.capture(command, REQUEST_BODY, REQUEST_BODY_TRUNCATED)

        if (GraftcodeTelemetrySettings.isAutoInjectEnabled() && !hasOwnTraceparent(connectionData)) {
            inject(trace.setSpan(parent, span), callContext.outgoingHeaders)
        }

        return callSpan
    }

    static startHandle(tracer, callContext, command, headers, settings) {
        const operation = tryResolveOperation(command, settings)
        if (!operation) {
            return null
        }

        const parent = extractParent(headers)
        const span = tracer.startSpan(
            operation.name,
            { kind: SpanKind.INTERNAL, attributes: handleAttributes(operation, command) },
            parent
        )
        const callSpan = new CallSpan(span, settings)
        callSpan.#pushScope(callContext, parent, span)
        callSpan.capture(command, REQUEST_BODY, REQUEST_BODY_TRUNCATED)
        return callSpan
    }

    complete(response, error) {
        if (this.#completed) {
            return
        }
        this.#completed = true

        if (response != null) {
            this.capture(response, RESPONSE_BODY, RESPONSE_BODY_TRUNCATED)
        }

        if (error == null) {
            if (isCommand(response) && response.commandType === CommandType.Exception) {
                const name = typeof response.payload[2] === 'string' ? response.payload[2] : 'Exception'
                const message = typeof response.payload[3] === 'string' ? response.payload[3] : ''
                markFailed(this.#span, name, message)
            } else {
                this.#span.setStatus({ code: SpanStatusCode.OK })
            }
        } else {
            const name = error instanceof Error ? error.name : typeof error
            const message = error instanceof Error ? error.message : String(error)
            const stack =
                error instanceof Error && error.stack ? LogsRedactor.redactBody(error.stack) : undefined
            markFailed(this.#span, name, message, stack)
        }

        this.#span.end()
    }

    capture(command, bodyKey, truncatedKey) {
        if (!this.#span.isRecording() || !GraftcodeTelemetrySettings.isBodyCaptureEnabled()) {
            return
        }
        const rendered = renderBody(command, GraftcodeTelemetrySettings.getBodyMaxCharacters())
        if (!rendered) {
            return
        }
        this.#span.setAttribute(bodyKey, rendered.body)
        if (rendered.truncated) {
            this.#span.setAttribute(truncatedKey, true)
        }
    }

    #pushScope(callContext, parent, span) {
        const active = trace.setSpan(parent, span)
        callContext.items.push(this)
        callContext.items.push({
            wrap(run) {
                return context.with(active, run)
            },
        })
    }
}

function tryResolveOperation(command, settings) {
    if (!isCommand(command) || SKIP_COMMAND_TYPES.has(command.commandType)) {
        return undefined
    }

    const operation = resolveOperation(command)
    if (!operation || settings.ignore?.(operation)) {
        return undefined
    }

    if (operation.name.toLowerCase().endsWith('telemetrysetup.initialize')) {
        return undefined
    }

    return operation
}

function invokeAttributes(operation, command, connectionData, target) {
    const attributes = handleAttributes(operation, command)
    const connectionType = safe(() => connectionData.connectionType)

    attributes[DEPENDENCY_TYPE] = BRIDGE_DEPENDENCY_TYPE
    attributes[DEPENDENCY_TARGET] = String(LogsRedactor.redactValue(target) ?? '')
    attributes['hypertube.target'] = safe(() => LogsRedactor.redactTarget(connectionData.hostname)) ?? ''
    if (typeof connectionType === 'number') {
        attributes['hypertube.connection_type'] = connectionTypeName(connectionType)
    }
    return attributes
}

function handleAttributes(operation, command) {
    return {
        'hypertube.runtime': runtimeNameOf(command.runtimeName),
        'hypertube.command_type': commandTypeName(command.commandType),
        'hypertube.operation': operation.name,
    }
}

function markFailed(span, name, message, stack) {
    const redacted = String(LogsRedactor.redactValue(message) ?? '')
    span.recordException({ name, message: redacted, stack })
    span.setAttribute('exception.type', name)
    if (redacted) {
        span.setAttribute('exception.message', redacted)
    }
    span.setStatus({ code: SpanStatusCode.ERROR, message: redacted })
}

function inject(active, headers) {
    propagation.inject(active, headers)
    const spanContext = trace.getSpanContext(active)
    if (spanContext && isSpanContextValid(spanContext)) {
        headers[TRACE_ID_HEADER] = spanContext.traceId
    }
}

function extractParent(headers) {
    const base = context.active()
    const extracted = propagation.extract(base, headers)
    const extractedSpan = trace.getSpanContext(extracted)
    if (extractedSpan && isSpanContextValid(extractedSpan) && extractedSpan !== trace.getSpanContext(base)) {
        return extracted
    }

    const traceId = readTraceId(headers[TRACE_ID_HEADER])
    if (!traceId) {
        return base
    }
    return trace.setSpanContext(base, {
        traceId,
        spanId: randomSpanId(),
        traceFlags: TraceFlags.SAMPLED,
        isRemote: true,
    })
}

function readTraceId(raw) {
    const hex = raw?.trim().replaceAll('-', '').toLowerCase()
    return hex && /^[0-9a-f]{32}$/.test(hex) && !/^0+$/.test(hex) ? hex : undefined
}

function randomSpanId() {
    let spanId
    do {
        spanId = randomBytes(8).toString('hex')
    } while (/^0+$/.test(spanId))
    return spanId
}

function hasOwnTraceparent(connectionData) {
    try {
        return TRACE_PARENT_HEADER in connectionData.getHeaders()
    } catch {
        return false
    }
}

function safe(read) {
    try {
        return read()
    } catch {
        return undefined
    }
}

export function completeSpans(callContext, response, error) {
    for (let i = callContext.items.length - 1; i >= 0; i--) {
        const item = callContext.items[i]
        if (item instanceof CallSpan) {
            item.complete(response, error)
            callContext.items.splice(i, 1)
        }
    }
}
