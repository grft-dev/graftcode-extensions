import assert from 'node:assert/strict'
import { createRequire } from 'node:module'
import { test } from 'node:test'
import { TraceFlags, context, propagation, trace } from '@opentelemetry/api'
import { useGraftAppInsights } from '../src/index.js'

const CONNECTION_STRING =
    'InstrumentationKey=00000000-0000-0000-0000-000000000000;IngestionEndpoint=http://127.0.0.1:9/;LiveEndpoint=http://127.0.0.1:9/'

test('with a connection string, exports to Azure Monitor and keeps every trace by default', async () => {
    const telemetry = useGraftAppInsights({
        serviceName: 'ConnectorAzureTest',
        connectionString: CONNECTION_STRING,
        console: false,
        azureMonitor: {
            enableLiveMetrics: false,
            enableStandardMetrics: false,
            enablePerformanceCounters: false,
            azureMonitorExporterOptions: { disableOfflineStorage: true },
        },
    })

    assert.equal(telemetry.isExporting, true)

    const spans = Array.from({ length: 50 }, (_, index) => telemetry.tracer.startSpan(`span ${index}`))
    spans.forEach((span) => span.end())
    assert.ok(spans.every((span) => span.isRecording() === false && span.spanContext().traceFlags === TraceFlags.SAMPLED))
    assert.equal(spans[0].resource.attributes['service.name'], 'ConnectorAzureTest')

    const carrier = {}
    propagation.inject(trace.setSpan(context.active(), spans[0]), carrier)
    assert.match(carrier.traceparent, /^00-[0-9a-f]{32}-[0-9a-f]{16}-01$/)

    const http = createRequire(import.meta.url)('node:http')
    const server = http.createServer((request, response) => {
        response.end(JSON.stringify(request.headers))
    })
    await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve))
    const { port } = server.address()
    const parent = telemetry.tracer.startSpan('parent')
    const [plain, upgrade] = await context.with(trace.setSpan(context.active(), parent), () =>
        Promise.all([
            requestHeaders(http, port, { traceparent: 'replaced' }),
            requestHeaders(http, port, { Connection: 'Upgrade', Upgrade: 'websocket', traceparent: 'kept' }),
        ])
    )
    parent.end()
    server.close()
    assert.match(plain.traceparent, new RegExp(`^00-${parent.spanContext().traceId}-`))
    assert.equal(upgrade.traceparent, 'kept')

    await telemetry.shutdown()
})

function requestHeaders(http, port, headers) {
    return new Promise((resolve, reject) => {
        const request = http.request({ host: '127.0.0.1', port, path: '/', headers }, (response) => {
            let body = ''
            response.on('data', (chunk) => (body += chunk))
            response.on('end', () => resolve(JSON.parse(body)))
        })
        request.on('upgrade', (response, socket) => {
            socket.destroy()
            reject(new Error('unexpected upgrade'))
        })
        request.on('error', reject)
        request.end()
    })
}
