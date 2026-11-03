import { shutdownAzureMonitor, useAzureMonitor } from '@azure/monitor-opentelemetry'
import { trace } from '@opentelemetry/api'
import { logs } from '@opentelemetry/api-logs'
import { registerInstrumentations } from '@opentelemetry/instrumentation'
import { resourceFromAttributes } from '@opentelemetry/resources'
import { NodeTracerProvider } from '@opentelemetry/sdk-trace-node'
import { GraftcodeInstrumentation } from 'graftcode-instrumentation'
import { LogsLevel, parseLogsLevel } from 'hypertube-nodejs-sdk/abstractions'
import { createAppLogger } from './appLogger.js'

let active

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

    let stopping
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

function startLocalTracing(serviceName) {
    const provider = new NodeTracerProvider({ resource: resourceFromAttributes({ 'service.name': serviceName }) })
    provider.register()
    return () => provider.shutdown()
}

function isWebSocketUpgrade(request) {
    const headers = request?.headers ?? {}
    return Object.entries(headers).some(
        ([name, value]) => name.toLowerCase() === 'upgrade' && String(value).toLowerCase() === 'websocket'
    )
}

async function forceFlush() {
    await Promise.allSettled([
        delegateOf(trace.getTracerProvider())?.forceFlush?.(),
        delegateOf(logs.getLoggerProvider())?.forceFlush?.(),
    ])
}

function delegateOf(provider) {
    if (typeof provider?.getDelegate === 'function') {
        return provider.getDelegate()
    }
    if (typeof provider?._getDelegate === 'function') {
        return provider._getDelegate()
    }
    return provider
}
