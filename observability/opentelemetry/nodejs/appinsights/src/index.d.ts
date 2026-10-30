import type { AzureMonitorOpenTelemetryOptions } from '@azure/monitor-opentelemetry'
import type { Tracer } from '@opentelemetry/api'
import type { Instrumentation } from '@opentelemetry/instrumentation'
import type { GraftcodeInstrumentationConfig } from 'graftcode-instrumentation'

export type LogAttributes = Record<string, string | number | boolean | undefined>

export interface GraftCodeAppInsightsOptions {
    serviceName: string
    connectionString?: string
    tracesPerSecond?: number
    samplingRatio?: number
    logLevel?: 'debug' | 'info' | 'warning' | 'error'
    console?: boolean
    graftcode?: GraftcodeInstrumentationConfig | false
    instrumentations?: Instrumentation[]
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
    readonly isExporting: boolean
    readonly tracer: Tracer
    readonly logger: GraftCodeAppLogger
    flush(): Promise<void>
    shutdown(): Promise<void>
}

export declare function useGraftCodeAppInsights(options: GraftCodeAppInsightsOptions): GraftCodeTelemetry
