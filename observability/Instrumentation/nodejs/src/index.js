export { GraftcodeInstrumentation } from './GraftcodeInstrumentation.js'
export { resolveOperation } from './operation.js'

/**
 * @typedef {Object} GraftcodeInstrumentationConfig
 * @property {boolean} [enabled=true]
 * @property {boolean} [captureBodies] - Record redacted request and response payloads on spans.
 *   Defaults to `HYPERTUBE_TELEMETRY_CAPTURE_BODIES`, otherwise `false`.
 * @property {number} [maxBodyLength] - Characters kept per payload, 256 to 32768.
 *   Defaults to `HYPERTUBE_TELEMETRY_BODY_MAX_CHARS`, otherwise 8192.
 * @property {boolean} [propagateContext] - Send W3C trace context with outgoing calls.
 *   Defaults to `HYPERTUBE_TELEMETRY_AUTO_INJECT`, otherwise `true`.
 * @property {'trace' | 'debug' | 'info' | 'warning' | 'error' | 'critical' | 'off'} [logLevel='info'] -
 *   Lowest Hypertube log level forwarded to OpenTelemetry logs.
 * @property {(operation: import('./operation.js').GraftOperation) => boolean} [ignore] - Return `true`
 *   to skip tracing an operation.
 */
