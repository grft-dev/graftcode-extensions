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
} from 'hypertube-nodejs-sdk/observability'
import { renderBody } from './bodyCapture.js'
import { resolveOperation } from './operation.js'

export const TRACEPARENT_HEADER = 'traceparent'
export const TRACEID_HEADER = 'traceid'

export const ATTR_REQUEST_BODY = 'hypertube.request.body'
export const ATTR_RESPONSE_BODY = 'hypertube.response.body'

/**
 * @typedef {import('@opentelemetry/api').Span} Span
 * @typedef {import('@opentelemetry/api').Tracer} Tracer
 * @typedef {import('@opentelemetry/api').Context} Context
 * @typedef {import('@opentelemetry/api').Attributes} Attributes
 * @typedef {import('hypertube-nodejs-sdk/observability').HypertubeMiddleware} HypertubeMiddleware
 * @typedef {import('./settings.js').ResolvedSettings} ResolvedSettings
 * @typedef {import('./operation.js').GraftOperation} GraftOperation
 */

/**
 * Creates the Hypertube middleware that wraps each remote call in a span: CLIENT on the calling side,
 * SERVER on the called side, following the OpenTelemetry RPC conventions.
 * @param {() => Tracer} getTracer
 * @param {() => ResolvedSettings} getSettings
 * @returns {HypertubeMiddleware}
 */
export function createTracingMiddleware(getTracer, getSettings) {
    return {
        name: 'GraftcodeInstrumentation',

        invoke(call, next) {
            const settings = getSettings()
            const command = LogsCommandResolver.resolveForInvoke(call.command)
            const operation = resolveOperation(command)
            if (!operation || settings.ignore?.(operation)) {
                return next()
            }

            const parent = context.active()
            const span = getTracer().startSpan(
                operation.name,
                { kind: SpanKind.CLIENT, attributes: clientAttributes(operation, command, call.connectionData) },
                parent
            )
            const active = trace.setSpan(parent, span)

            if (settings.propagateContext && !hasOwnTraceparent(call.connectionData)) {
                inject(active, call.headers)
            }
            captureBody(span, settings, command, ATTR_REQUEST_BODY)

            return settle(span, settings, () => context.with(active, next))
        },

        handle(call, next) {
            const settings = getSettings()
            const command = LogsCommandResolver.resolveForHandle(call.command)
            const operation = resolveOperation(command)
            if (!operation || settings.ignore?.(operation)) {
                return next()
            }

            const parent = extractParent(call.headers)
            const span = getTracer().startSpan(
                operation.name,
                { kind: SpanKind.SERVER, attributes: commonAttributes(operation, command) },
                parent
            )
            const active = trace.setSpan(parent, span)
            captureBody(span, settings, command, ATTR_REQUEST_BODY)

            return settle(span, settings, () => context.with(active, next))
        },
    }
}

/**
 * @param {Span} span
 * @param {ResolvedSettings} settings
 * @param {() => unknown} run
 * @returns {Promise<void> | void}
 */
function settle(span, settings, run) {
    /** @type {unknown} */
    let result
    try {
        result = run()
    } catch (error) {
        endWithError(span, error)
        return
    }

    if (result !== null && typeof result === 'object' && 'then' in result && typeof result.then === 'function') {
        return Promise.resolve(result).then(
            (response) => endWithResponse(span, settings, response),
            (error) => endWithError(span, error)
        )
    }
    endWithResponse(span, settings, result)
}

/**
 * A Hypertube call that fails on the remote side still completes with an `Exception` response,
 * so the span is marked as failed from that response.
 * @param {Span} span
 * @param {ResolvedSettings} settings
 * @param {unknown} response
 */
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

/**
 * @param {Span} span
 * @param {unknown} error
 */
function endWithError(span, error) {
    const name = error instanceof Error ? error.name : typeof error
    const message = error instanceof Error ? error.message : String(error)
    const stack = error instanceof Error && error.stack ? LogsRedactor.redactBody(error.stack) : undefined
    markFailed(span, name, message, stack)
    span.end()
}

/**
 * @param {Span} span
 * @param {string} name
 * @param {string} message
 * @param {string} [stack]
 */
function markFailed(span, name, message, stack) {
    const redacted = String(LogsRedactor.redactValue(message) ?? '')
    span.recordException({ name, message: redacted, stack })
    span.setAttribute('error.type', name)
    span.setStatus({ code: SpanStatusCode.ERROR, message: redacted })
}

/**
 * @param {Context} active
 * @param {Record<string, string>} headers
 */
function inject(active, headers) {
    propagation.inject(active, headers)
    const spanContext = trace.getSpanContext(active)
    if (spanContext && isSpanContextValid(spanContext)) {
        headers[TRACEID_HEADER] = spanContext.traceId
    }
}

/**
 * Uses W3C `traceparent`/`tracestate` when present. Callers that only send a Graftcode `traceid`
 * header still join that trace, under a synthetic remote parent.
 * @param {Readonly<Record<string, string>>} headers
 * @returns {Context}
 */
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

/**
 * @param {string | undefined} raw
 * @returns {string | undefined}
 */
function readTraceId(raw) {
    const hex = raw?.trim().replaceAll('-', '').toLowerCase()
    return hex && /^[0-9a-f]{32}$/.test(hex) && !/^0+$/.test(hex) ? hex : undefined
}

/**
 * @returns {string}
 */
function randomSpanId() {
    let spanId
    do {
        spanId = randomBytes(8).toString('hex')
    } while (/^0+$/.test(spanId))
    return spanId
}

/**
 * A `traceparent` configured on the connection itself is the caller's choice and is not overridden.
 * @param {{ getHeaders(): Record<string, string> }} connectionData
 * @returns {boolean}
 */
function hasOwnTraceparent(connectionData) {
    try {
        return TRACEPARENT_HEADER in connectionData.getHeaders()
    } catch {
        return false
    }
}

/**
 * @param {Span} span
 * @param {ResolvedSettings} settings
 * @param {unknown} command
 * @param {string} attribute
 */
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

/**
 * @param {GraftOperation} operation
 * @param {{ runtimeName: number, commandType: number }} command
 * @returns {Attributes}
 */
function commonAttributes(operation, command) {
    /** @type {Attributes} */
    const attributes = {
        'rpc.system': 'graftcode',
        'rpc.method': operation.method,
        'hypertube.runtime': runtimeNameOf(command.runtimeName),
        'hypertube.command_type': commandTypeName(command.commandType),
    }
    if (operation.service) {
        attributes['rpc.service'] = operation.service
    }
    return attributes
}

/**
 * @param {GraftOperation} operation
 * @param {{ runtimeName: number, commandType: number }} command
 * @param {{ hostname?: string, connectionType?: number }} connectionData
 * @returns {Attributes}
 */
function clientAttributes(operation, command, connectionData) {
    const attributes = commonAttributes(operation, command)
    const target = safe(() => LogsRedactor.redactTarget(connectionData.hostname)) ?? ''
    const connectionType = safe(() => connectionData.connectionType)

    attributes['graft.dependency.type'] = 'Graft'
    attributes['graft.dependency.target'] = target
    attributes['hypertube.target'] = target
    if (typeof connectionType === 'number') {
        attributes['hypertube.connection_type'] = connectionTypeName(connectionType)
    }

    const url = safe(() => new URL(target))
    if (url?.hostname) {
        attributes['server.address'] = url.hostname
        const port = Number(url.port || defaultPort(url.protocol))
        if (port) {
            attributes['server.port'] = port
        }
    }
    return attributes
}

/**
 * @param {string} protocol
 * @returns {number}
 */
function defaultPort(protocol) {
    return protocol === 'wss:' || protocol === 'https:' ? 443 : protocol === 'ws:' || protocol === 'http:' ? 80 : 0
}

/**
 * @template T
 * @param {() => T} read
 * @returns {T | undefined}
 */
function safe(read) {
    try {
        return read()
    } catch {
        return undefined
    }
}
