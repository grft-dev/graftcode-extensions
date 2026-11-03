import { createRequire } from 'node:module'
import { context } from '@opentelemetry/api'
import { InstrumentationBase } from '@opentelemetry/instrumentation'
import {
    HypertubeBroker,
    HypertubeLogger,
    LogsCommandResolver,
    LogsLevel,
    LogsRedactor,
} from 'hypertube-nodejs-sdk/abstractions'
import { CallSpan, completeSpans } from './callSpan.js'
import { resolveInstrumentationOptions } from './graftcodeTelemetrySettings.js'

const { version: PACKAGE_VERSION } = createRequire(import.meta.url)('../package.json')

export const INSTRUMENTATION_NAME = 'Graftcode'
export const LOGGER_NAME = 'Hypertube'

const SEVERITIES = [
    [1, 'TRACE'],
    [5, 'DEBUG'],
    [9, 'INFO'],
    [13, 'WARN'],
    [17, 'ERROR'],
    [21, 'FATAL'],
]

export class GraftcodeInstrumentation extends InstrumentationBase {
    constructor(config = {}) {
        super(INSTRUMENTATION_NAME, PACKAGE_VERSION, config)
    }

    init() {
        return []
    }

    enable() {
        if (this._detachFromHypertube) {
            return
        }

        const getSettings = () => this._currentSettings()
        const listener = {
            onExecute: (callContext, command, connectionData) => {
                const reported = LogsCommandResolver.resolveForInvoke(command)
                CallSpan.startInvoke(this.tracer, callContext, reported, connectionData, getSettings())
            },
            onResponse: (callContext, response) => {
                completeSpans(callContext, response, null)
            },
            onInvocationError: (callContext, error) => {
                completeSpans(callContext, null, error)
            },
            onProcess: (callContext, command) => {
                const headers = {}
                const reported = LogsCommandResolver.resolveForHandle(command, headers)
                CallSpan.startHandle(this.tracer, callContext, reported, headers, getSettings())
            },
            onProcessed: (callContext, response) => {
                completeSpans(callContext, response, null)
            },
            onHandlingError: (callContext, error) => {
                completeSpans(callContext, null, error)
            },
        }

        const logBridge = {
            isEnabled: (level) => level < LogsLevel.Off && level >= getSettings().logLevel,

            log: (record) => {
                const [severityNumber, severityText] = SEVERITIES[record.level] ?? SEVERITIES[LogsLevel.Info]
                const attributes = { ...record.properties }

                if (record.error !== undefined && record.error !== null) {
                    const error = record.error
                    attributes['exception.type'] = error instanceof Error ? error.name : typeof error
                    attributes['exception.message'] = String(
                        LogsRedactor.redactValue(error instanceof Error ? error.message : String(error)) ?? ''
                    )
                    if (error instanceof Error && error.stack) {
                        attributes['exception.stacktrace'] = LogsRedactor.redactBody(error.stack)
                    }
                }

                this.logger.emit({
                    severityNumber,
                    severityText,
                    body: record.message,
                    attributes,
                    context: context.active(),
                })
            },
        }

        const detachBroker = HypertubeBroker.attach(listener)
        const detachLogs = HypertubeLogger.attach(logBridge)
        this._detachFromHypertube = () => {
            detachBroker()
            detachLogs()
        }
    }

    disable() {
        this._detachFromHypertube?.()
        this._detachFromHypertube = undefined
    }

    _currentSettings() {
        const config = this.getConfig()
        if (config !== this._settingsSource || !this._settings) {
            this._settingsSource = config
            this._settings = resolveInstrumentationOptions(config)
        }
        return this._settings
    }
}
