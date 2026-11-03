import { LogsLevel, parseLogsLevel } from 'hypertube-nodejs-sdk/abstractions'

export const MINIMUM_BODY_MAX_CHARS = 256
export const DEFAULT_BODY_MAX_CHARS = 8192
export const MAXIMUM_BODY_MAX_CHARS = 32768

let autoInject = readFlag('HYPERTUBE_TELEMETRY_AUTO_INJECT', true)
let captureBodies = readFlag('HYPERTUBE_TELEMETRY_CAPTURE_BODIES', false)
let bodyMaxCharacters = readBodyMaxChars()

export function isAutoInjectEnabled() {
    return autoInject
}

export function setAutoInjectEnabled(value) {
    autoInject = value
}

export function isBodyCaptureEnabled() {
    return captureBodies
}

export function setBodyCaptureEnabled(value) {
    captureBodies = value
}

export function getBodyMaxCharacters() {
    return bodyMaxCharacters
}

export function setBodyMaxCharacters(value) {
    bodyMaxCharacters = clampBodyMaxCharacters(value)
}

export function clampBodyMaxCharacters(value) {
    if (value < MINIMUM_BODY_MAX_CHARS) {
        return MINIMUM_BODY_MAX_CHARS
    }
    return Math.min(value, MAXIMUM_BODY_MAX_CHARS)
}

export function applyInstrumentationConfig(config) {
    if (config.propagateContext !== undefined) {
        setAutoInjectEnabled(config.propagateContext)
    }
    if (config.captureBodies !== undefined) {
        setBodyCaptureEnabled(config.captureBodies)
    }
    if (config.maxBodyLength !== undefined) {
        setBodyMaxCharacters(config.maxBodyLength)
    }
}

export function resolveInstrumentationOptions(config) {
    applyInstrumentationConfig(config)
    return {
        logLevel: parseLogsLevel(config.logLevel ?? 'info') ?? LogsLevel.Info,
        ignore: typeof config.ignore === 'function' ? config.ignore : undefined,
        libraries:
            typeof config.libraries === 'function' || (config.libraries && typeof config.libraries === 'object')
                ? config.libraries
                : undefined,
    }
}

function readBodyMaxChars() {
    const value = Number.parseInt(process.env.HYPERTUBE_TELEMETRY_BODY_MAX_CHARS ?? '', 10)
    return Number.isFinite(value) ? clampBodyMaxCharacters(value) : DEFAULT_BODY_MAX_CHARS
}

function readFlag(name, defaultValue) {
    const value = process.env[name]?.trim().toLowerCase()
    if (!value) {
        return defaultValue
    }
    return value !== 'false' && value !== '0'
}
