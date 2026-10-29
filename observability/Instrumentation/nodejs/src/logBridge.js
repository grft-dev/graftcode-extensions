import { context } from '@opentelemetry/api'
import { LogsLevel, LogsRedactor } from 'hypertube-nodejs-sdk/observability'

/**
 * OpenTelemetry severity numbers and texts, indexed by Hypertube log level.
 */
const SEVERITIES = [
    [1, 'TRACE'],
    [5, 'DEBUG'],
    [9, 'INFO'],
    [13, 'WARN'],
    [17, 'ERROR'],
    [21, 'FATAL'],
]

/**
 * @typedef {import('@opentelemetry/api-logs').Logger} OtelLogger
 * @typedef {import('hypertube-nodejs-sdk/observability').HypertubeLogListener} HypertubeLogListener
 * @typedef {import('./settings.js').ResolvedSettings} ResolvedSettings
 */

/**
 * Forwards Hypertube's own log records to the OpenTelemetry Logs API, correlated with the active span.
 * @param {() => OtelLogger} getLogger
 * @param {() => ResolvedSettings} getSettings
 * @returns {HypertubeLogListener}
 */
export function createLogBridge(getLogger, getSettings) {
    return {
        isEnabled: (level) => level < LogsLevel.Off && level >= getSettings().logLevel,

        log(record) {
            const [severityNumber, severityText] = SEVERITIES[record.level] ?? SEVERITIES[LogsLevel.Info]
            /** @type {Record<string, string>} */
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

            getLogger().emit({
                severityNumber,
                severityText,
                body: record.message,
                attributes,
                context: context.active(),
            })
        },
    }
}
