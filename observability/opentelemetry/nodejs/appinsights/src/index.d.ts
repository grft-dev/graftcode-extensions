import type { AzureMonitorOpenTelemetryOptions } from '@azure/monitor-opentelemetry'
import type { Tracer } from '@opentelemetry/api'
import type { Instrumentation } from '@opentelemetry/instrumentation'
import type { GraftcodeInstrumentationConfig } from 'graftcode-instrumentation'

export type LogAttributes = Record<string, string | number | boolean | undefined>

export interface GraftCodeAppInsightsOptions {
    /** Shown as the cloud role name in Application Insights. */
    serviceName: string
    /** Defaults to `APPLICATIONINSIGHTS_CONNECTION_STRING`. */
    connectionString?: string
    /** Rate-limit sampling. By default every trace is kept. */
    tracesPerSecond?: number
    /** Fixed-ratio sampling between 0 and 1, used when `tracesPerSecond` is not set. */
    samplingRatio?: number
    /** Lowest level exported for application and Graftcode logs. Defaults to `info`. */
    logLevel?: 'debug' | 'info' | 'warning' | 'error'
    /** Also print application logs to standard output. Defaults to `true`. */
    console?: boolean
    /** Graftcode instrumentation options, or `false` to leave it out. */
    graftcode?: GraftcodeInstrumentationConfig | false
    /** Extra OpenTelemetry instrumentations to register. */
    instrumentations?: Instrumentation[]
    /** Advanced Azure Monitor options, applied first. */
    azureMonitor?: AzureMonitorOpenTelemetryOptions
}

export interface GraftCodeAppLogger {
    debug(message: string, attributes?: LogAttributes): void
    info(message: string, attributes?: LogAttributes): void
    warn(message: string, attributes?: LogAttributes): void
    error(message: string, errorOrAttributes?: Error | LogAttributes): void
}

export interface GraftCodeTelemetry {
    readonly serviceName: string
    /** Whether telemetry is sent to Application Insights. */
    readonly isExporting: boolean
    readonly tracer: Tracer
    readonly logger: GraftCodeAppLogger
    /** Sends buffered telemetry now. */
    flush(): Promise<void>
    /** Flushes and stops telemetry. Safe to call more than once. */
    shutdown(): Promise<void>
}

/**
 * Starts OpenTelemetry with the Azure Monitor exporters and Graftcode instrumentation. Call it once,
 * as early as possible (for a web server, before the HTTP framework is imported, e.g. via `node --import`).
 */
export declare function useGraftCodeAppInsights(options: GraftCodeAppInsightsOptions): GraftCodeTelemetry
