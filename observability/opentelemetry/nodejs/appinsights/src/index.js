import { shutdownAzureMonitor, useAzureMonitor } from '@azure/monitor-opentelemetry'
import { trace } from '@opentelemetry/api'
import { logs } from '@opentelemetry/api-logs'
import { registerInstrumentations } from '@opentelemetry/instrumentation'
import { resourceFromAttributes } from '@opentelemetry/resources'
import { NodeTracerProvider } from '@opentelemetry/sdk-trace-node'
import { GraftcodeInstrumentation } from 'graftcode-instrumentation'
import { LogsLevel, parseLogsLevel } from 'hypertube-nodejs-sdk/observability'
import { createAppLogger } from './appLogger.js'

/**
 * @typedef {Object} GraftCodeAppInsightsOptions
 * @property {string} serviceName - Shown as the cloud role name in Application Insights.
 * @property {string} [connectionString] - Defaults to `APPLICATIONINSIGHTS_CONNECTION_STRING`.
 * @property {number} [tracesPerSecond] - Rate-limit sampling. By default every trace is kept.
 * @property {number} [samplingRatio] - Fixed-ratio sampling between 0 and 1, used when `tracesPerSecond` is not set.
 * @property {'debug' | 'info' | 'warning' | 'error'} [logLevel='info'] - Lowest level exported for application and Graftcode logs.
 * @property {boolean} [console=true] - Also print application logs to standard output.
 * @property {import('graftcode-instrumentation').GraftcodeInstrumentationConfig | false} [graftcode] - Graftcode instrumentation options, or `false` to leave it out.
 * @property {import('@opentelemetry/instrumentation').Instrumentation[]} [instrumentations] - Extra OpenTelemetry instrumentations to register.
 * @property {import('@azure/monitor-opentelemetry').AzureMonitorOpenTelemetryOptions} [azureMonitor] - Advanced Azure Monitor options, applied first.
 */

/**
 * @typedef {Object} GraftCodeAppLogger
 * @property {(message: string, attributes?: Record<string, string | number | boolean | undefined>) => void} debug
 * @property {(message: string, attributes?: Record<string, string | number | boolean | undefined>) => void} info
 * @property {(message: string, attributes?: Record<string, string | number | boolean | undefined>) => void} warn
 * @property {(message: string, errorOrAttributes?: Error | Record<string, string | number | boolean | undefined>) => void} error
 */

/**
 * @typedef {Object} GraftCodeTelemetry
 * @property {string} serviceName
 * @property {boolean} isExporting - Whether telemetry is sent to Application Insights.
 * @property {import('@opentelemetry/api').Tracer} tracer
 * @property {GraftCodeAppLogger} logger
 * @property {() => Promise<void>} flush - Sends buffered telemetry now.
 * @property {() => Promise<void>} shutdown - Flushes and stops telemetry. Safe to call more than once.
 */

/** @type {GraftCodeTelemetry | undefined} */
let active

/**
 * Starts OpenTelemetry with the Azure Monitor exporters and Graftcode instrumentation. Call it once,
 * as early as possible (for a web server, before the HTTP framework is imported, e.g. via `node --import`).
 * @param {GraftCodeAppInsightsOptions} options
 * @returns {GraftCodeTelemetry}
 */
export function useGraftCodeAppInsights(options) {
    const serviceName = options?.serviceName?.trim()
    if (!serviceName) {
        throw new TypeError('useGraftCodeAppInsights needs a serviceName')
    }
    if (active) {
        if (active.serviceName === serviceName) {
            return active
        }
        throw new Error(`GraftCode Application Insights telemetry is already started for ${active.serviceName}`)
    }

    const connectionString =
        options.connectionString?.trim() || process.env.APPLICATIONINSIGHTS_CONNECTION_STRING?.trim() || undefined
    const logLevel = parseLogsLevel(options.logLevel ?? 'info') ?? LogsLevel.Info
    const stopProviders = connectionString
        ? startAzureMonitor(serviceName, connectionString, options)
        : startLocalTracing(serviceName)

    const graftcode =
        options.graftcode === false
            ? []
            : [new GraftcodeInstrumentation({ logLevel: options.logLevel ?? 'info', ...options.graftcode })]
    const unregisterInstrumentations = registerInstrumentations({
        instrumentations: [...graftcode, ...(options.instrumentations ?? [])],
    })

    /** @type {Promise<void> | undefined} */
    let stopping
    /** @type {GraftCodeTelemetry} */
    const telemetry = {
        serviceName,
        isExporting: connectionString !== undefined,
        tracer: trace.getTracer(serviceName),
        logger: createAppLogger(serviceName, logLevel, options.console !== false),
        flush: forceFlush,
        shutdown: () => {
            stopping ??= (async () => {
                process.off('beforeExit', onBeforeExit)
                await forceFlush()
                unregisterInstrumentations()
                await stopProviders()
                active = undefined
            })()
            return stopping
        },
    }
    const onBeforeExit = () => {
        void telemetry.shutdown()
    }
    process.once('beforeExit', onBeforeExit)

    active = telemetry
    if (!connectionString) {
        telemetry.logger.warn(
            `Application Insights telemetry for ${serviceName} is disabled: no connection string was configured. ` +
                'Set APPLICATIONINSIGHTS_CONNECTION_STRING in this process or pass connectionString.'
        )
    }
    return telemetry
}

/**
 * @param {string} serviceName
 * @param {string} connectionString
 * @param {GraftCodeAppInsightsOptions} options
 * @returns {() => Promise<void>}
 */
function startAzureMonitor(serviceName, connectionString, options) {
    const advanced = options.azureMonitor ?? {}
    const http = advanced.instrumentationOptions?.http
    const userIgnoreOutgoing = http?.ignoreOutgoingRequestHook

    useAzureMonitor({
        ...advanced,
        azureMonitorExporterOptions: { ...advanced.azureMonitorExporterOptions, connectionString },
        resource: resourceFromAttributes({ 'service.name': serviceName }),
        tracesPerSecond: options.tracesPerSecond ?? 0,
        samplingRatio: options.samplingRatio ?? 1,
        instrumentationOptions: {
            ...advanced.instrumentationOptions,
            http: {
                enabled: true,
                ...http,
                ignoreOutgoingRequestHook: (request) =>
                    isWebSocketUpgrade(request) || Boolean(userIgnoreOutgoing?.(request)),
            },
        },
    })
    return () => shutdownAzureMonitor()
}

/**
 * Without a connection string nothing is exported, but spans still get real trace ids so trace
 * context keeps flowing through Graftcode calls.
 * @param {string} serviceName
 * @returns {() => Promise<void>}
 */
function startLocalTracing(serviceName) {
    const provider = new NodeTracerProvider({ resource: resourceFromAttributes({ 'service.name': serviceName }) })
    provider.register()
    return () => provider.shutdown()
}

/**
 * A WebSocket handshake opens a long-lived connection shared by many calls. Tracing it as an HTTP
 * request would attach it to whichever call happened to open it, so it is left out; the Graftcode
 * span already describes each call.
 * @param {import('node:http').RequestOptions} request
 * @returns {boolean}
 */
function isWebSocketUpgrade(request) {
    const headers = request?.headers ?? {}
    return Object.entries(headers).some(
        ([name, value]) => name.toLowerCase() === 'upgrade' && String(value).toLowerCase() === 'websocket'
    )
}

/**
 * @returns {Promise<void>}
 */
async function forceFlush() {
    await Promise.allSettled([
        delegateOf(trace.getTracerProvider())?.forceFlush?.(),
        delegateOf(logs.getLoggerProvider())?.forceFlush?.(),
    ])
}

/**
 * @param {any} provider
 * @returns {any}
 */
function delegateOf(provider) {
    if (typeof provider?.getDelegate === 'function') {
        return provider.getDelegate()
    }
    if (typeof provider?._getDelegate === 'function') {
        return provider._getDelegate()
    }
    return provider
}
