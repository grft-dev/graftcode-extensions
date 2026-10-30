import type { InstrumentationBase, InstrumentationConfig } from '@opentelemetry/instrumentation'

export interface GraftOperation {
    name: string
    service?: string
    method: string
    commandType: string
}

export interface LibraryTarget {
    target: string
    channel: string
    runtime: string
    service?: string
    method: string
}

export type LibraryResolver = Record<string, string> | ((target: LibraryTarget) => string | undefined)

export interface GraftcodeInstrumentationConfig extends InstrumentationConfig {
    captureBodies?: boolean
    maxBodyLength?: number
    propagateContext?: boolean
    logLevel?: 'trace' | 'debug' | 'info' | 'warning' | 'error' | 'critical' | 'off'
    ignore?: (operation: GraftOperation) => boolean
    libraries?: LibraryResolver
}

export declare class GraftcodeInstrumentation extends InstrumentationBase<GraftcodeInstrumentationConfig> {
    constructor(config?: GraftcodeInstrumentationConfig)
    protected init(): []
    enable(): void
    disable(): void
}

export declare function resolveOperation(command: unknown): GraftOperation | undefined
