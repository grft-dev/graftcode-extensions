import { context } from '@opentelemetry/api'
import { LogsLevel, LogsRedactor } from 'hypertube-nodejs-sdk/abstractions'

const SEVERITIES = [
    [1, 'TRACE'],
    [5, 'DEBUG'],
    [9, 'INFO'],
    [13, 'WARN'],
    [17, 'ERROR'],
    [21, 'FATAL'],
]

export function createLogBridge(getLogger, getSettings) {
    return {
        isEnabled: (level) => level < LogsLevel.Off && level >= getSettings().logLevel,

        log(record) {
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
