import { context } from '@opentelemetry/api'
import { logs } from '@opentelemetry/api-logs'
import { LogsLevel, LogsRedactor } from 'hypertube-nodejs-sdk/observability'

const SEVERITIES = {
    [LogsLevel.Debug]: [5, 'DEBUG', 'dbug'],
    [LogsLevel.Info]: [9, 'INFO', 'info'],
    [LogsLevel.Warning]: [13, 'WARN', 'warn'],
    [LogsLevel.Error]: [17, 'ERROR', 'fail'],
}

/**
 * @typedef {Record<string, string | number | boolean | undefined>} LogAttributes
 * @typedef {import('./index.js').GraftAppLogger} GraftAppLogger
 */

/**
 * @param {string} serviceName
 * @param {number} minimumLevel
 * @param {boolean} toConsole
 * @returns {GraftAppLogger}
 */
export function createAppLogger(serviceName, minimumLevel, toConsole) {
    const logger = logs.getLogger(serviceName)

    /**
     * @param {number} level
     * @param {string} message
     * @param {LogAttributes | Error | undefined} details
     */
    const write = (level, message, details) => {
        if (level < minimumLevel) {
            return
        }

        const [severityNumber, severityText, label] = SEVERITIES[level]
        const attributes = details instanceof Error ? exceptionAttributes(details) : { ...details }
        logger.emit({ severityNumber, severityText, body: message, attributes, context: context.active() })

        if (toConsole) {
            const suffix = details instanceof Error ? ` ${details.name}: ${details.message}` : ''
            process.stdout.write(`${label}: ${serviceName}: ${message}${suffix}\n`)
        }
    }

    return {
        debug: (message, attributes) => write(LogsLevel.Debug, message, attributes),
        info: (message, attributes) => write(LogsLevel.Info, message, attributes),
        warn: (message, attributes) => write(LogsLevel.Warning, message, attributes),
        error: (message, errorOrAttributes) => write(LogsLevel.Error, message, errorOrAttributes),
    }
}

/**
 * @param {Error} error
 * @returns {LogAttributes}
 */
function exceptionAttributes(error) {
    return {
        'exception.type': error.name,
        'exception.message': String(LogsRedactor.redactValue(error.message) ?? ''),
        'exception.stacktrace': error.stack ? LogsRedactor.redactBody(error.stack) : undefined,
    }
}
