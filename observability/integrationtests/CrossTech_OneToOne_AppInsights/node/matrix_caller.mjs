import { readFileSync } from 'node:fs'
import { dirname, join, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'
import { SpanKind } from '@opentelemetry/api'
import { RuntimeBridge, WsConnectionData } from 'hypertube-nodejs-sdk'
import { useGraftCodeAppInsights } from 'graftcode-appinsights-connector'

const root = resolve(dirname(fileURLToPath(import.meta.url)), '..')
const configPath = process.argv[2] ?? join(root, 'matrix-config.json')

if (!process.env.APPLICATIONINSIGHTS_CONNECTION_STRING) {
    console.error('APPLICATIONINSIGHTS_CONNECTION_STRING is not set')
    process.exit(2)
}

const hosts = JSON.parse(readFileSync(configPath, 'utf8')).hosts
const graftLibraries = Object.fromEntries(
    Object.values(hosts).map((callee) => [callee.ws, callee.serviceName]),
)

const telemetry = useGraftCodeAppInsights({
    serviceName: 'CrossTech.OneToOne.NodeCaller',
    connectionString: process.env.APPLICATIONINSIGHTS_CONNECTION_STRING,
    graftcode: { libraries: graftLibraries },
})

function selectRuntime(factory, name) {
    switch (name) {
        case 'netcore':
            return factory.netcore()
        case 'jvm':
            return factory.jvm()
        case 'python':
            return factory.python()
        default:
            return factory.nodejs()
    }
}

for (const [calleeKey, callee] of Object.entries(hosts)) {
    const input = `matrix-node-${calleeKey.toLowerCase()}`
    let traceId = ''
    let result = ''
    try {
        await telemetry.tracer.startActiveSpan(
            `CrossMatrix.Node->${calleeKey}`,
            { kind: SpanKind.SERVER },
            async (span) => {
                try {
                    traceId = span.spanContext().traceId
                    telemetry.logger.info(
                        `CrossMatrix Caller=Node Callee=${calleeKey} Input=${input} TraceId=${traceId}`,
                    )

                    const runtime = selectRuntime(
                        RuntimeBridge.webSocket(new WsConnectionData(callee.ws)),
                        callee.runtimeArg,
                    )
                    result = await runtime
                        .getType(callee.typeName)
                        .invokeStaticMethod(callee.methodName, input)
                        .execute()
                        .then((r) => r.getValue())

                    telemetry.logger.info(`CrossMatrix after call Result=${result}`)
                } finally {
                    span.end()
                }
            },
        )
        await telemetry.flush()
        console.log(`OK|Node|${calleeKey}|${traceId}|${String(result).replaceAll('|', '/')}`)
    } catch (error) {
        console.log(`FAIL|Node|${calleeKey}||${error.name}: ${String(error.message).replaceAll('|', '/')}`)
    }
}

await telemetry.shutdown()