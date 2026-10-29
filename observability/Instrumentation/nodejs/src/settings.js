import { LogsLevel, parseLogsLevel } from 'hypertube-nodejs-sdk/observability'

const DEFAULT_MAX_BODY_LENGTH = 8192
const MIN_BODY_LENGTH = 256
const MAX_BODY_LENGTH = 32768

/**
 * @typedef {import('./index.js').GraftcodeInstrumentationConfig} GraftcodeInstrumentationConfig
 * @typedef {import('./operation.js').GraftOperation} GraftOperation
 */

/**
 * @typedef {Object} ResolvedSettings
 * @property {boolean} captureBodies
 * @property {number} maxBodyLength
 * @property {boolean} propagateContext
 * @property {number} logLevel
 * @property {((operation: GraftOperation) => boolean) | undefined} ignore
 */

/**
 * Options set in code win over environment variables, which win over defaults.
 * @param {GraftcodeInstrumentationConfig} config
 * @returns {ResolvedSettings}
 */
export function resolveSettings(config) {
    return {
        captureBodies: config.captureBodies ?? readFlag('HYPERTUBE_TELEMETRY_CAPTURE_BODIES', false),
        maxBodyLength: clamp(
            config.maxBodyLength ?? readInteger('HYPERTUBE_TELEMETRY_BODY_MAX_CHARS', DEFAULT_MAX_BODY_LENGTH),
            MIN_BODY_LENGTH,
            MAX_BODY_LENGTH
        ),
        propagateContext: config.propagateContext ?? readFlag('HYPERTUBE_TELEMETRY_AUTO_INJECT', true),
        logLevel: parseLogsLevel(config.logLevel ?? 'info') ?? LogsLevel.Info,
        ignore: typeof config.ignore === 'function' ? config.ignore : undefined,
    }
}

/**
 * @param {string} name
 * @param {boolean} defaultValue
 * @returns {boolean}
 */
function readFlag(name, defaultValue) {
    const value = process.env[name]?.trim().toLowerCase()
    if (!value) {
        return defaultValue
    }
    return value !== 'false' && value !== '0'
}

/**
 * @param {string} name
 * @param {number} defaultValue
 * @returns {number}
 */
function readInteger(name, defaultValue) {
    const value = Number.parseInt(process.env[name] ?? '', 10)
    return Number.isFinite(value) ? value : defaultValue
}

/**
 * @param {number} value
 * @param {number} min
 * @param {number} max
 * @returns {number}
 */
function clamp(value, min, max) {
    return Math.min(max, Math.max(min, Math.trunc(value)))
}
