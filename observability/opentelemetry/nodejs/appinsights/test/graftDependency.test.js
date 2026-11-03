import assert from 'node:assert/strict'
import { createRequire } from 'node:module'
import { dirname, join } from 'node:path'
import { test } from 'node:test'
import { RuntimeBridge } from 'hypertube-nodejs-sdk'
import { useGraftCodeAppInsights } from '../src/index.js'

const require = createRequire(import.meta.url)
const { readableSpanToEnvelope } = require(
    join(dirname(require.resolve('@azure/monitor-opentelemetry-exporter')), 'utils', 'spanUtils.js')
)

class RandomService {
    static fetch(input) {
        return `fetched ${input}`
    }
}
globalThis.RandomService = RandomService

test('Application Insights shows a Graftcode call as a Graft <library> <channel> InProc dependency, like .NET and Java', async () => {
    const finished = []
    const telemetry = useGraftCodeAppInsights({
        serviceName: 'ConnectorGraftDependencyTest',
        connectionString:
            'InstrumentationKey=00000000-0000-0000-0000-000000000000;IngestionEndpoint=http://127.0.0.1:9/;LiveEndpoint=http://127.0.0.1:9/',
        console: false,
        graftcode: { libraries: { inmemory: 'TestGgNodeLib' } },
        azureMonitor: {
            enableLiveMetrics: false,
            enableStandardMetrics: false,
            enablePerformanceCounters: false,
            azureMonitorExporterOptions: { disableOfflineStorage: true },
            spanProcessors: [
                {
                    onStart() {},
                    onEnd: (span) => finished.push(span),
                    forceFlush: async () => {},
                    shutdown: async () => {},
                },
            ],
        },
    })

    try {
        const result = await telemetry.tracer.startActiveSpan('GET /api/fetch/:input', async (span) => {
            try {
                const runtime = RuntimeBridge.inMemory().nodejs()
                return (await runtime.getType('RandomService').invokeStaticMethod('fetch', 'x').execute()).getValue()
            } finally {
                span.end()
            }
        })
        assert.equal(result, 'fetched x')

        const dependencies = finished
            .map((span) => readableSpanToEnvelope(span, '00000000-0000-0000-0000-000000000000').data)
            .filter((data) => data.baseType === 'RemoteDependencyData')
            .map((data) => data.baseData)

        const caller = dependencies.find((data) => data.name === 'Graft TestGgNodeLib inmemory')
        const called = dependencies.find((data) => data.name === 'RandomService.fetch')
        assert.ok(caller, `no Graft dependency in ${JSON.stringify(dependencies.map((data) => data.name))}`)
        assert.ok(called)
        assert.equal(caller.type, 'InProc')
        assert.equal(called.type, 'InProc')
        assert.equal(caller.properties['graft.dependency.type'], 'Graft')
        assert.equal(dependencies.some((data) => data.type === 'GRPC'), false)
    } finally {
        await telemetry.shutdown()
    }
})
