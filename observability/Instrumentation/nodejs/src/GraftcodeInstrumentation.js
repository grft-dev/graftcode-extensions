import { createRequire } from 'node:module'
import { InstrumentationBase } from '@opentelemetry/instrumentation'
import { HypertubeLogger, HypertubePipeline } from 'hypertube-nodejs-sdk/observability'
import { createLogBridge } from './logBridge.js'
import { resolveSettings } from './settings.js'
import { createTracingMiddleware } from './spans.js'

const { name: PACKAGE_NAME, version: PACKAGE_VERSION } = createRequire(import.meta.url)('../package.json')

/**
 * @typedef {import('./index.js').GraftcodeInstrumentationConfig} GraftcodeInstrumentationConfig
 * @typedef {import('./settings.js').ResolvedSettings} ResolvedSettings
 */

/**
 * Traces Graftcode calls and forwards Hypertube logs to OpenTelemetry.
 *
 * Hypertube exposes a middleware pipeline, so nothing is monkey-patched: enabling registers a
 * middleware and a log listener, disabling removes them. Register it like any other instrumentation:
 *
 * ```js
 * registerInstrumentations({ instrumentations: [new GraftcodeInstrumentation()] })
 * ```
 * @extends {InstrumentationBase<GraftcodeInstrumentationConfig>}
 */
export class GraftcodeInstrumentation extends InstrumentationBase {
    /**
     * @param {GraftcodeInstrumentationConfig} [config]
     */
    constructor(config = {}) {
        super(PACKAGE_NAME, PACKAGE_VERSION, config)
    }

    init() {
        return []
    }

    enable() {
        if (this._detachFromHypertube) {
            return
        }

        const getSettings = () => this._currentSettings()
        const removeMiddleware = HypertubePipeline.use(createTracingMiddleware(() => this.tracer, getSettings))
        const detachLogs = HypertubeLogger.attach(createLogBridge(() => this.logger, getSettings))
        this._detachFromHypertube = () => {
            removeMiddleware()
            detachLogs()
        }
    }

    disable() {
        this._detachFromHypertube?.()
        this._detachFromHypertube = undefined
    }

    /**
     * @returns {ResolvedSettings}
     */
    _currentSettings() {
        const config = this.getConfig()
        if (config !== this._settingsSource || !this._settings) {
            this._settingsSource = config
            this._settings = resolveSettings(config)
        }
        return this._settings
    }
}
