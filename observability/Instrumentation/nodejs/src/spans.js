import { randomBytes } from 'node:crypto'
import { SpanKind, SpanStatusCode, TraceFlags, context, isSpanContextValid, propagation, trace } from '@opentelemetry/api'
import {
    CommandType,
    LogsCommandResolver,
    LogsRedactor,
    commandTypeName,
    connectionTypeName,
    isCommand,
    runtimeNameOf,
} from 'hypertube-nodejs-sdk/abstractions'
import { renderBody } from './bodyCapture.js'
import { describeTarget, recordLibraries } from './libraryCatalog.js'
import { resolveOperation } from './operation.js'

export const TRACEPARENT_HEADER = 'traceparent'
export const TRACEID_HEADER = 'traceid'

export const ATTR_REQUEST_BODY = 'hypertube.request.body'
export const ATTR_RESPONSE_BODY = 'hypertube.response.body'
export const ATTR_DEPENDENCY_TYPE = 'graft.dependency.type'
export const ATTR_DEPENDENCY_TARGET = 'graft.dependency.target'
export const DEPENDENCY_TYPE = 'Graft'

class LiveSpan {
    constructor(span, settings) {
        this.span = span
        this.settings = settings
    }

    complete(response, error) {
        if (error) {
            endWithError(this.span, error)
            return
        }
        endWithResponse(this.span, this.settings, response)
    }
}

export function createTracingListener(getTracer, getSettings) {
    return {
        onExecute(callContext, command, connectionData) {
            const settings = getSettings()
            const reported = LogsCommandResolver.resolveForInvoke(command)
            recordLibraries(reported, connectionData)
            const operation = resolveOperation(reported)
            if (!operation || settings.ignore?.(operation)) {
                return
            }

            const target = describeTarget(reported, connectionData, operation, settings.libraries)
            const parent = context.active()
            const span = getTracer().startSpan(
                `Graft ${target}`,
                {
                    kind: SpanKind.INTERNAL,
                    attributes: callerAttributes(operation, reported, connectionData, target),
                },
                parent
            )
            const active = trace.setSpan(parent, span)
            callContext.items.push(new LiveSpan(span, settings))
            callContext.items.push({
                wrap(run) {
                    return context.with(active, run)
                },
            })

            if (settings.propagateContext && !hasOwnTraceparent(connectionData)) {
                inject(active, callContext.outgoingHeaders)
            }
            captureBody(span, settings, reported, ATTR_REQUEST_BODY)
        },

        onResponse(callContext, response) {
            completeLiveSpans(callContext, response, null)
        },

        onInvocationError(callContext, error) {
            completeLiveSpans(callContext, null, error)
        },

        onProcess(callContext, command) {
            const settings = getSettings()
            const headers = {}
            const reported = LogsCommandResolver.resolveForHandle(command, headers)
            const operation = resolveOperation(reported)
            if (!operation || settings.ignore?.(operation)) {
                return
            }

            const parent = extractParent(headers)
            const span = getTracer().startSpan(
                operation.name,
                { kind: SpanKind.INTERNAL, attributes: commonAttributes(operation, reported) },
                parent
            )
            const active = trace.setSpan(parent, span)
            callContext.items.push(new LiveSpan(span, settings))
            callContext.items.push({
                wrap(run) {
                    return context.with(active, run)
                },
            })
            captureBody(span, settings, reported, ATTR_REQUEST_BODY)
        },

        onProcessed(callContext, response) {
            completeLiveSpans(callContext, response, null)
        },

        onHandlingError(callContext, error) {
            completeLiveSpans(callContext, null, error)
        },
    }
}

function completeLiveSpans(callContext, response, error) {
    for (let i = callContext.items.length - 1; i >= 0; i--) {
        const item = callContext.items[i]
        if (item instanceof LiveSpan) {
            item.complete(response, error)
            callContext.items.splice(i, 1)
        }
    }
}

function endWithResponse(span, settings, response) {
    if (isCommand(response)) {
        captureBody(span, settings, response, ATTR_RESPONSE_BODY)
        if (response.commandType === CommandType.Exception) {
            const name = typeof response.payload[2] === 'string' ? response.payload[2] : 'Exception'
            const message = typeof response.payload[3] === 'string' ? response.payload[3] : ''
            markFailed(span, name, message)
        }
    }
    span.end()
}

function endWithError(span, error) {
    const name = error instanceof Error ? error.name : typeof error
    const message = error instanceof Error ? error.message : String(error)
    const stack = error instanceof Error && error.stack ? LogsRedactor.redactBody(error.stack) : undefined
    markFailed(span, name, message, stack)
    span.end()
}

function markFailed(span, name, message, stack) {
    const redacted = String(LogsRedactor.redactValue(message) ?? '')
    span.recordException({ name, message: redacted, stack })
    span.setAttribute('error.type', name)
    span.setStatus({ code: SpanStatusCode.ERROR, message: redacted })
}

function inject(active, headers) {
    propagation.inject(active, headers)
    const spanContext = trace.getSpanContext(active)
    if (spanContext && isSpanContextValid(spanContext)) {
        headers[TRACEID_HEADER] = spanContext.traceId
    }
}

function extractParent(headers) {
    const base = context.active()
    const extracted = propagation.extract(base, headers)
    const extractedSpan = trace.getSpanContext(extracted)
    if (extractedSpan && isSpanContextValid(extractedSpan) && extractedSpan !== trace.getSpanContext(base)) {
        return extracted
    }

    const traceId = readTraceId(headers[TRACEID_HEADER])
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
        return TRACEPARENT_HEADER in connectionData.getHeaders()
    } catch {
        return false
    }
}

function captureBody(span, settings, command, attribute) {
    if (!settings.captureBodies || !span.isRecording()) {
        return
    }
    const rendered = renderBody(command, settings.maxBodyLength)
    if (!rendered) {
        return
    }
    span.setAttribute(attribute, rendered.body)
    if (rendered.truncated) {
        span.setAttribute(`${attribute}.truncated`, true)
    }
}

function commonAttributes(operation, command) {
    return {
        'hypertube.runtime': runtimeNameOf(command.runtimeName),
        'hypertube.command_type': commandTypeName(command.commandType),
        'hypertube.operation': operation.name,
    }
}

function callerAttributes(operation, command, connectionData, target) {
    const attributes = commonAttributes(operation, command)
    const connectionType = safe(() => connectionData.connectionType)

    attributes[ATTR_DEPENDENCY_TYPE] = DEPENDENCY_TYPE
    attributes[ATTR_DEPENDENCY_TARGET] = String(LogsRedactor.redactValue(target) ?? '')
    attributes['hypertube.target'] = safe(() => LogsRedactor.redactTarget(connectionData.hostname)) ?? ''
    if (typeof connectionType === 'number') {
        attributes['hypertube.connection_type'] = connectionTypeName(connectionType)
    }
    return attributes
}

function safe(read) {
    try {
        return read()
    } catch {
        return undefined
    }
}
