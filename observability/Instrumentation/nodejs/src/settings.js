import { LogsLevel, parseLogsLevel } from 'hypertube-nodejs-sdk/abstractions'

const DEFAULT_MAX_BODY_LENGTH = 8192
const MIN_BODY_LENGTH = 256
const MAX_BODY_LENGTH = 32768

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
        libraries:
            typeof config.libraries === 'function' || (config.libraries && typeof config.libraries === 'object')
                ? config.libraries
                : undefined,
    }
}

function readFlag(name, defaultValue) {
    const value = process.env[name]?.trim().toLowerCase()
    if (!value) {
        return defaultValue
    }
    return value !== 'false' && value !== '0'
}

function readInteger(name, defaultValue) {
    const value = Number.parseInt(process.env[name] ?? '', 10)
    return Number.isFinite(value) ? value : defaultValue
}

function clamp(value, min, max) {
    return Math.min(max, Math.max(min, Math.trunc(value)))
}
