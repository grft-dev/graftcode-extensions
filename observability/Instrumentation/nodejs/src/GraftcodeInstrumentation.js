import { createRequire } from 'node:module'
import { InstrumentationBase } from '@opentelemetry/instrumentation'
import { HypertubeBroker, HypertubeLogger } from 'hypertube-nodejs-sdk/abstractions'
import { createLogBridge } from './logBridge.js'
import { resolveSettings } from './settings.js'
import { createTracingListener } from './spans.js'

const { name: PACKAGE_NAME, version: PACKAGE_VERSION } = createRequire(import.meta.url)('../package.json')

export class GraftcodeInstrumentation extends InstrumentationBase {
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
        const detachBroker = HypertubeBroker.attach(createTracingListener(() => this.tracer, getSettings))
        const detachLogs = HypertubeLogger.attach(createLogBridge(() => this.logger, getSettings))
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
            this._settings = resolveSettings(config)
        }
        return this._settings
    }
}
