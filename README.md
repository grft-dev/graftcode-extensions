# graftcode-extensions

Official open-source Graftcode Gateway plugins. Each plugin carries Graft calls over an external channel instead of the Gateway's built-in servers. The Gateway and the calling runtime select a plugin by configuration; they do not share code.

## Pick a plugin

| Plugin | Channel | Modes | Local setup | Docs |
|--------|---------|-------|-------------|------|
| [rabbitmq](rabbitmq/Readme.md) | RabbitMQ (AMQP 0-9-1) | request/reply | [samples/rabbitmq](samples/rabbitmq) | [Readme](rabbitmq/Readme.md) |
| [servicebus](servicebus/Readme.md) | Azure Service Bus (AMQP 1.0) | request/reply and one-way | [samples/servicebus](samples/servicebus) | [Readme](servicebus/Readme.md) |
| [kafka](kafka/Readme.md) | Apache Kafka | request/reply | [samples/kafka](samples/kafka) | [Readme](kafka/Readme.md) |
| [sqs](sqs/Readme.md) | Amazon SQS | request/reply and one-way | [samples/sqs](samples/sqs) (LocalStack) | [Readme](sqs/Readme.md) |
| [pubsub](pubsub/Readme.md) | Google Cloud Pub/Sub | request/reply and one-way | [samples/pubsub](samples/pubsub) (Pub/Sub emulator) | [Readme](pubsub/Readme.md) |

All local Docker setups, ports, and startup instructions are documented in
[samples/README.md](samples/README.md). Each setup starts its middleware and
three Gateway servers consuming from the same request destination.

OpenTelemetry / Application Insights lives under [observability/opentelemetry](observability/opentelemetry/dotnet/Graft.Netcore.Telemetry.AppInsightsConnector/README.md). It is not a transport plugin.

Use RabbitMQ or Kafka when you already run that broker. Use Service Bus or SQS when the deployment is already on Azure or AWS. Use Pub/Sub when the deployment is already on Google Cloud. SQS and Pub/Sub each need one reply destination per client process; Service Bus can share one session-enabled reply queue.

## Release archives

Each release archive is flat and contains the plugin shared library. Third-party libraries are linked into that library. A client does not install OpenSSL, curl, zlib, the AWS SDK, the Azure SDK, or the VC++ Redistributable to load these plugins.

- **Windows.** The archive is the plugin DLL only. The MSVC CRT is static (`/MT`). `ucrtbase.dll` and other Windows system libraries stay on the machine.
- **Linux and macOS.** The archive is the plugin `.so` or `.dylib` only. glibc, libstdc++, libc++, and libSystem stay on the host.
- **Linux Kafka.** The archive also contains `libsasl2.so.2` and `libsasl2.so.2.0.25`. Cyrus SASL loads mechanism modules at runtime, so that library stays shared. Copy those files next to `libKafkaPlugin.so`.

`name` in the config is the library file name without the extension: `SqsPlugin` for `SqsPlugin.dll`, `libSqsPlugin` for `libSqsPlugin.so`.

## Use a plugin

1. Download the release archive for the platform, or build the plugin (see its Readme).
2. Put `gg`, the hosted module, and the plugin library in one folder. For Linux Kafka, also put `libsasl2.so.2` and `libsasl2.so.2.0.25` from the same archive there.
3. Start the Gateway with `--config` and one of the JSON files below. Verified local Docker layouts are available under [samples](samples/README.md).
4. Call the Gateway from .NET with the Hypertube SDK (see [Client configuration](#client-configuration)), or open `http://localhost:81/GV` and install the generated package.

How the Gateway loads a plugin is described in the "Plugin server config" section of the [Graftcode Gateway](https://github.com/grft-dev/graftcode-gateway) README.

## Gateway config

The Gateway `--config` file is the plugin object. The calling runtime wraps that same object in `configurations` → package name → `plugin`. Linux and macOS use a `lib` prefix on `name` (`libRabbitmqPlugin`). Field lists are in each plugin Readme.

RabbitMQ (`rabbitmq/Readme.md`):

```json
{
  "name": "RabbitmqPlugin",
  "host": "localhost",
  "port": 5672,
  "queue": "myqueue",
  "replyQueue": "myqueue.reply",
  "user": "guest",
  "password": "guest",
  "vhost": "/",
  "rpcTimeoutMs": 30000
}
```

Service Bus (`servicebus/Readme.md`):

```json
{
  "name": "ServiceBusPlugin",
  "connectionString": "Endpoint=sb://<namespace>.servicebus.windows.net/;SharedAccessKeyName=<keyName>;SharedAccessKey=<key>",
  "queue": "myqueue",
  "replyQueue": "myqueue.reply",
  "rpcTimeoutMs": 30000
}
```

Kafka (`kafka/Readme.md`):

```json
{
  "name": "KafkaPlugin",
  "brokers": "localhost:9092",
  "requestTopic": "graft.requests",
  "replyTopic": "graft.replies",
  "groupId": "graft-gateway",
  "rpcTimeoutMs": 30000
}
```

SQS (`sqs/Readme.md`, LocalStack):

```json
{
  "name": "SqsPlugin",
  "region": "us-east-1",
  "endpointOverride": "http://localhost:4566",
  "accessKeyId": "test",
  "secretAccessKey": "test",
  "requestQueueUrl": "http://sqs.us-east-1.localhost.localstack.cloud:4566/000000000000/graft-requests",
  "waitTimeSeconds": 10,
  "visibilityTimeoutSeconds": 60,
  "verifySsl": false
}
```

Pub/Sub (`pubsub/Readme.md`, emulator):

```json
{
  "name": "PubSubPlugin",
  "projectId": "graftcode-local",
  "emulatorHost": "localhost:8085",
  "requestTopic": "graft-requests",
  "ackDeadlineSeconds": 60,
  "autoCreate": true,
  "verifySsl": false
}
```

## Client configuration

A .NET client can call the gateway directly through the Hypertube SDK. The client config wraps the plugin object in `configurations` → package name → `plugin`, with no `runtime` or `host` keys; the plugin carries the transport.

```csharp
using Hypertube.Netcore.Sdk;
using Hypertube.Netcore.Sdk.Configuration;

RuntimeBridge.AddConfig(ConfigPriority.User, configJsonOrPathToFile);
var rtmCtx = RuntimeBridge.InitializeRC("graft.nuget.netapp");
var result = rtmCtx.GetType("netapp.Calculator").CreateInstance()
    .InvokeInstanceMethod("Add", 1, 2).Execute().GetValue();
```

`AddConfig` accepts a JSON string, a path to a JSON file, or the name of an environment variable holding either. The key under `configurations` must match the name passed to `InitializeRC`. Load one plugin config per process.

| Plugin | Client config | `name` |
|---|---|---|
| RabbitMQ | [rabbitmq/Readme.md](rabbitmq/Readme.md#client-configuration) | `RabbitmqPlugin` |
| Kafka | [kafka/Readme.md](kafka/Readme.md#client-configuration) | `KafkaPlugin` |
| Service Bus | [servicebus/Readme.md](servicebus/Readme.md#client-configuration) | `ServiceBusPlugin` |
| SQS | [sqs/Readme.md](sqs/Readme.md#client-configuration) | `SqsPlugin` |
| Pub/Sub | [pubsub/Readme.md](pubsub/Readme.md#client-configuration) | `PubSubPlugin` |

Part of the [Graftcode](https://github.com/grft-dev/graftcode) project.
