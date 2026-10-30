import assert from 'node:assert/strict'
import { fileURLToPath } from 'node:url'
import { afterEach, beforeEach, describe, test } from 'node:test'
import { SpanKind, SpanStatusCode, context, trace } from '@opentelemetry/api'
import { InMemoryLogRecordExporter, LoggerProvider, SimpleLogRecordProcessor } from '@opentelemetry/sdk-logs'
import { InMemorySpanExporter, SimpleSpanProcessor } from '@opentelemetry/sdk-trace-base'
import { NodeTracerProvider } from '@opentelemetry/sdk-trace-node'
import { RuntimeBridge } from 'hypertube-nodejs-sdk'
import { GraftcodeInstrumentation } from '../src/index.js'
import { resetLibraryCatalog } from '../src/libraryCatalog.js'

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

const spans = () => spanExporter.getFinishedSpans()
const callerSpan = () => spans().find((span) => span.attributes['graft.dependency.type'] === 'Graft')
const calledSpan = () =>
    spans().find((span) => span.attributes['hypertube.operation'] && !span.attributes['graft.dependency.type'])

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
        resetLibraryCatalog()
        RequestContext.current.setHeaders({})
    })

    afterEach(() => {
        instrumentation?.disable()
    })

    test('traces a call like .NET and Java: Graft <runtime> <channel> on the caller, Service.method on the called side', async () => {
        enable()

        assert.equal(await call('fetch', 'x'), 'fetched x')

        const caller = callerSpan()
        const called = calledSpan()
        assert.equal(caller.name, 'Graft Nodejs inmemory')
        assert.equal(called.name, 'RandomService.fetch')
        assert.equal(caller.kind, SpanKind.INTERNAL)
        assert.equal(called.kind, SpanKind.INTERNAL)
        assert.equal(called.spanContext().traceId, caller.spanContext().traceId)
        assert.equal(called.parentSpanContext?.spanId, caller.spanContext().spanId)
        assert.deepEqual(
            {
                dependencyType: caller.attributes['graft.dependency.type'],
                dependencyTarget: caller.attributes['graft.dependency.target'],
                operation: caller.attributes['hypertube.operation'],
                runtime: caller.attributes['hypertube.runtime'],
                commandType: caller.attributes['hypertube.command_type'],
                connectionType: caller.attributes['hypertube.connection_type'],
            },
            {
                dependencyType: 'Graft',
                dependencyTarget: 'Nodejs inmemory',
                operation: 'RandomService.fetch',
                runtime: 'Nodejs',
                commandType: 'InvokeStaticMethod',
                connectionType: 'InMemory',
            }
        )
        for (const span of spans()) {
            assert.equal(
                Object.keys(span.attributes).some((key) => key.startsWith('rpc.')),
                false,
                'rpc.* makes Azure Monitor report the call as gRPC'
            )
        }
        assert.equal(caller.status.code, SpanStatusCode.UNSET)
        assert.equal(caller.attributes['hypertube.request.body'], undefined)
    })

    test('names the library from the libraries option, as a map or a function', async () => {
        enable({ libraries: { inmemory: 'TestGgNodeLib' } })
        await call('fetch', 'x')
        assert.equal(callerSpan().name, 'Graft TestGgNodeLib inmemory')
        assert.equal(callerSpan().attributes['graft.dependency.target'], 'TestGgNodeLib inmemory')

        instrumentation.disable()
        spanExporter.reset()
        const seen = []
        enable({
            libraries: (target) => {
                seen.push(target)
                return 'FromFunction'
            },
        })
        await call('fetch', 'x')
        assert.equal(callerSpan().name, 'Graft FromFunction inmemory')
        assert.deepEqual(seen[0], {
            target: 'inmemory',
            channel: 'inmemory',
            runtime: 'Nodejs',
            service: 'RandomService',
            method: 'fetch',
        })
    })

    test('names the library and version from a library loaded on the connection', async () => {
        enable()
        const libraryPath = fileURLToPath(new URL('./fixtures/calc-lib', import.meta.url))

        const runtime = await nodejs().loadLibrary(libraryPath)
        const sum = (await runtime.getType('Calculator').invokeStaticMethod('add', 2, 3).execute()).getValue()

        assert.equal(sum, 5)
        assert.equal(callerSpan().name, 'Graft calc-lib 2.3.4 inmemory')
        assert.equal(calledSpan().name, 'Calculator.add')
    })

    test('joins the active trace and delivers the trace id to RequestContext on the called side', async () => {
        enable()
        const parent = trace.getTracer('test').startSpan('GET /api/fetch/:input')

        const traceId = await context.with(trace.setSpan(context.active(), parent), () => call('traceId'))
        parent.end()

        assert.equal(callerSpan().parentSpanContext?.spanId, parent.spanContext().spanId)
        assert.equal(traceId, parent.spanContext().traceId)
    })

    test('marks both spans as failed when the remote method throws, with the message redacted', async () => {
        enable()

        await assert.rejects(call('fail'))

        for (const span of [callerSpan(), calledSpan()]) {
            assert.equal(span.status.code, SpanStatusCode.ERROR)
            assert.equal(span.attributes['error.type'], 'TypeError')
            const exception = span.events.find((event) => event.name === 'exception')
            assert.equal(exception.attributes['exception.message'], '[REDACTED]')
        }
    })

    test('forwards Hypertube logs to OpenTelemetry logs, correlated with the call spans', async () => {
        enable()

        await call('fetch', 'y')

        const client = callerSpan()
        const server = calledSpan()
        const records = logExporter.getFinishedLogRecords()
        const invoking = records.find((record) => String(record.body).startsWith('Hypertube invoking'))
        const handling = records.find((record) => String(record.body).startsWith('Hypertube handling'))
        const handled = records.find((record) => String(record.body).startsWith('Hypertube handled'))
        const completed = records.find((record) => String(record.body).startsWith('Hypertube completed'))

        assert.equal(invoking.body, 'Hypertube invoking InvokeStaticMethod on the Nodejs runtime')
        assert.equal(invoking.severityText, 'INFO')
        assert.equal(invoking.attributes.CommandType, 'InvokeStaticMethod')
        assert.equal(invoking.spanContext?.spanId, client.spanContext().spanId)
        assert.equal(completed.spanContext?.spanId, client.spanContext().spanId)
        assert.equal(handling.spanContext?.spanId, server.spanContext().spanId)
        assert.equal(handled.spanContext?.spanId, server.spanContext().spanId)
    })

    test('captures redacted request and response bodies only when enabled', async () => {
        enable({ captureBodies: true })

        await call('fetch', 'Bearer abcdefghijklmnop')

        const client = callerSpan()
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
