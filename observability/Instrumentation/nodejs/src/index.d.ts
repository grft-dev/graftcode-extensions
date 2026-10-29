import type { InstrumentationBase, InstrumentationConfig } from '@opentelemetry/instrumentation'

export interface GraftOperation {
    /** Span name: `Service/method`, or `method` for global functions. */
    name: string
    /** The remote type, e.g. `RandomService`. */
    service?: string
    /** The remote member, e.g. `fetch`. */
    method: string
    /** e.g. `InvokeStaticMethod`. */
    commandType: string
}

export interface GraftcodeInstrumentationConfig extends InstrumentationConfig {
    /** Record redacted request and response payloads on spans. Defaults to `HYPERTUBE_TELEMETRY_CAPTURE_BODIES`, otherwise `false`. */
    captureBodies?: boolean
    /** Characters kept per payload, 256 to 32768. Defaults to `HYPERTUBE_TELEMETRY_BODY_MAX_CHARS`, otherwise 8192. */
    maxBodyLength?: number
    /** Send W3C trace context with outgoing calls. Defaults to `HYPERTUBE_TELEMETRY_AUTO_INJECT`, otherwise `true`. */
    propagateContext?: boolean
    /** Lowest Hypertube log level forwarded to OpenTelemetry logs. Defaults to `info`. */
    logLevel?: 'trace' | 'debug' | 'info' | 'warning' | 'error' | 'critical' | 'off'
    /** Return `true` to skip tracing an operation. */
    ignore?: (operation: GraftOperation) => boolean
}

export declare class GraftcodeInstrumentation extends InstrumentationBase<GraftcodeInstrumentationConfig> {
    constructor(config?: GraftcodeInstrumentationConfig)
    protected init(): []
    enable(): void
    disable(): void
}

export declare function resolveOperation(command: unknown): GraftOperation | undefined
