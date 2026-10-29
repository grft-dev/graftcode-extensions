import assert from 'node:assert/strict'
import { afterEach, beforeEach, describe, test } from 'node:test'
import { SpanKind, SpanStatusCode, context, trace } from '@opentelemetry/api'
import { InMemoryLogRecordExporter, LoggerProvider, SimpleLogRecordProcessor } from '@opentelemetry/sdk-logs'
import { InMemorySpanExporter, SimpleSpanProcessor } from '@opentelemetry/sdk-trace-base'
import { NodeTracerProvider } from '@opentelemetry/sdk-trace-node'
import { RuntimeBridge } from 'hypertube-nodejs-sdk'
import { GraftcodeInstrumentation } from '../src/index.js'

const spanExporter = new InMemorySpanExporter()
const tracerProvider = new NodeTracerProvider({ spanProcessors: [new SimpleSpanProcessor(spanExporter)] })
tracerProvider.register()

const logExporter = new InMemoryLogRecordExporter()
const loggerProvider = new LoggerProvider({ processors: [new SimpleLogRecordProcessor({ exporter: logExporter })] })

class RequestContext {
    static current = new RequestContext()
    headers = {}
    getHeaders() {
        return { ...this.headers }
    }
    setHeaders(headers) {
        this.headers = { ...headers }
    }
}

class RandomService {
    static fetch(input) {
        return `fetched ${input}`
    }
    static traceId() {
        return RequestContext.current.getHeaders().traceid
    }
    static fail() {
        throw new TypeError('rejected because password=hunter2 is wrong')
    }
}

globalThis.RequestContext = RequestContext
globalThis.RandomService = RandomService

const nodejs = () => RuntimeBridge.inMemory().nodejs()
const call = async (method, ...args) =>
    (await nodejs().getType('RandomService').invokeStaticMethod(method, ...args).execute()).getValue()

const spanByKind = (kind) => spanExporter.getFinishedSpans().find((span) => span.kind === kind)

describe('GraftcodeInstrumentation', () => {
    /** @type {GraftcodeInstrumentation} */
    let instrumentation

    const enable = (config) => {
        instrumentation = new GraftcodeInstrumentation(config)
        instrumentation.setTracerProvider(tracerProvider)
        instrumentation.setLoggerProvider(loggerProvider)
    }

    beforeEach(() => {
        spanExporter.reset()
        logExporter.reset()
        RequestContext.current.setHeaders({})
    })

    afterEach(() => {
        instrumentation?.disable()
    })

    test('traces a call as a CLIENT span on the caller and a SERVER span on the called side', async () => {
        enable()

        assert.equal(await call('fetch', 'x'), 'fetched x')

        const client = spanByKind(SpanKind.CLIENT)
        const server = spanByKind(SpanKind.SERVER)
        assert.equal(client.name, 'RandomService/fetch')
        assert.equal(server.name, 'RandomService/fetch')
        assert.equal(server.spanContext().traceId, client.spanContext().traceId)
        assert.equal(server.parentSpanContext?.spanId, client.spanContext().spanId)
        assert.deepEqual(
            {
                system: client.attributes['rpc.system'],
                service: client.attributes['rpc.service'],
                method: client.attributes['rpc.method'],
                runtime: client.attributes['hypertube.runtime'],
                commandType: client.attributes['hypertube.command_type'],
                dependencyType: client.attributes['graft.dependency.type'],
            },
            {
                system: 'graftcode',
                service: 'RandomService',
                method: 'fetch',
                runtime: 'Nodejs',
                commandType: 'InvokeStaticMethod',
                dependencyType: 'Graft',
            }
        )
        assert.equal(client.status.code, SpanStatusCode.UNSET)
        assert.equal(client.attributes['hypertube.request.body'], undefined)
    })

    test('joins the active trace and delivers the trace id to RequestContext on the called side', async () => {
        enable()
        const parent = trace.getTracer('test').startSpan('GET /api/fetch/:input')

        const traceId = await context.with(trace.setSpan(context.active(), parent), () => call('traceId'))
        parent.end()

        const client = spanByKind(SpanKind.CLIENT)
        assert.equal(client.parentSpanContext?.spanId, parent.spanContext().spanId)
        assert.equal(traceId, parent.spanContext().traceId)
    })

    test('marks both spans as failed when the remote method throws, with the message redacted', async () => {
        enable()

        await assert.rejects(call('fail'))

        for (const span of [spanByKind(SpanKind.CLIENT), spanByKind(SpanKind.SERVER)]) {
            assert.equal(span.status.code, SpanStatusCode.ERROR)
            assert.equal(span.attributes['error.type'], 'TypeError')
            const exception = span.events.find((event) => event.name === 'exception')
            assert.equal(exception.attributes['exception.message'], '[REDACTED]')
        }
    })

    test('forwards Hypertube logs to OpenTelemetry logs, correlated with the call spans', async () => {
        enable()

        await call('fetch', 'y')

        const client = spanByKind(SpanKind.CLIENT)
        const server = spanByKind(SpanKind.SERVER)
        const records = logExporter.getFinishedLogRecords()
        const invoking = records.find((record) => String(record.body).startsWith('Hypertube invoking'))
        const handling = records.find((record) => String(record.body).startsWith('Hypertube handling'))

        assert.equal(invoking.body, 'Hypertube invoking InvokeStaticMethod on the Nodejs runtime')
        assert.equal(invoking.severityText, 'INFO')
        assert.equal(invoking.attributes.CommandType, 'InvokeStaticMethod')
        assert.equal(invoking.spanContext?.spanId, client.spanContext().spanId)
        assert.equal(handling.spanContext?.spanId, server.spanContext().spanId)
    })

    test('captures redacted request and response bodies only when enabled', async () => {
        enable({ captureBodies: true })

        await call('fetch', 'Bearer abcdefghijklmnop')

        const client = spanByKind(SpanKind.CLIENT)
        const requestBody = client.attributes['hypertube.request.body']
        const request = JSON.parse(requestBody)
        assert.equal(request.type, 'InvokeStaticMethod')
        assert.equal(request.payload[0].type, 'GetType')
        assert.match(requestBody, /"fetch"/)
        assert.match(requestBody, /\[REDACTED\]/)
        assert.doesNotMatch(requestBody, /abcdefghijklmnop/)
        assert.match(client.attributes['hypertube.response.body'], /"type":"Value"/)
    })

    test('sends no trace headers to the called side when propagateContext is false', async () => {
        enable({ propagateContext: false })

        assert.equal(await call('traceId'), undefined)
        assert.deepEqual(RequestContext.current.getHeaders(), {})
    })

    test('skips operations rejected by ignore and stops tracing when disabled', async () => {
        enable({ ignore: (operation) => operation.method === 'fetch' })
        await call('fetch', 'z')
        assert.equal(spanExporter.getFinishedSpans().length, 0)

        instrumentation.disable()
        await call('traceId')
        assert.equal(spanExporter.getFinishedSpans().length, 0)
    })
})
