import assert from 'node:assert/strict'
import { test } from 'node:test'
import { isSpanContextValid } from '@opentelemetry/api'
import { HypertubePipeline } from 'hypertube-nodejs-sdk/observability'
import { useGraftAppInsights } from '../src/index.js'

test('without a connection string, traces locally, warns, and still registers Graftcode instrumentation', async () => {
    delete process.env.APPLICATIONINSIGHTS_CONNECTION_STRING
    const written = []
    const originalWrite = process.stdout.write
    process.stdout.write = (chunk, ...rest) => {
        written.push(String(chunk))
        return originalWrite.call(process.stdout, chunk, ...rest)
    }

    let telemetry
    try {
        telemetry = useGraftAppInsights({ serviceName: 'ConnectorLocalTest' })
    } finally {
        process.stdout.write = originalWrite
    }

    assert.equal(telemetry.isExporting, false)
    assert.ok(written.some((line) => line.startsWith('warn: ConnectorLocalTest: Application Insights telemetry')))
    assert.equal(useGraftAppInsights({ serviceName: 'ConnectorLocalTest' }), telemetry)
    assert.throws(() => useGraftAppInsights({ serviceName: 'Other' }), /already started/)
    assert.ok(HypertubePipeline.middleware.some((middleware) => middleware.name === 'GraftcodeInstrumentation'))

    const span = telemetry.tracer.startSpan('work')
    span.end()
    assert.ok(isSpanContextValid(span.spanContext()))
    assert.equal(span.resource.attributes['service.name'], 'ConnectorLocalTest')

    await telemetry.shutdown()
    await telemetry.shutdown()
    assert.ok(!HypertubePipeline.middleware.some((middleware) => middleware.name === 'GraftcodeInstrumentation'))
})
