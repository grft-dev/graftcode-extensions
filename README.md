# graftcode-extensions

Official open-source Graftcode Gateway plugins. Each plugin carries Graft calls over an external channel instead of the Gateway's built-in servers. The Gateway and the calling runtime select a plugin by configuration; they do not share code.

## Pick a plugin

| Plugin | Channel | Modes | Local setup | Docs |
|--------|---------|-------|-------------|------|
| [rabbitmq](rabbitmq/Readme.md) | RabbitMQ (AMQP 0-9-1) | request/reply | Docker image in `rabbitmq/` | [Readme](rabbitmq/Readme.md) |
| [servicebus](servicebus/Readme.md) | Azure Service Bus (AMQP 1.0) | request/reply and one-way | Service Bus emulator | [Readme](servicebus/Readme.md) |
| [kafka](kafka/Readme.md) | Apache Kafka | request/reply | [samples/kafka](samples/kafka/README.md) | [Readme](kafka/Readme.md) |
| [sqs](sqs/Readme.md) | Amazon SQS | request/reply and one-way | [samples/sqs](samples/sqs/README.md) (LocalStack) | [Readme](sqs/Readme.md) |
| [pubsub](pubsub/Readme.md) | Google Cloud Pub/Sub | request/reply and one-way | [samples/pubsub](samples/pubsub/README.md) (Pub/Sub emulator) | [Readme](pubsub/Readme.md) |

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
3. Start the Gateway with `--config` and one of the JSON files below. A verified SQS layout is in [samples/sqs](samples/sqs/README.md): `gg netapp.dll --config sqsplugin.json`.
4. Open `http://localhost:81/GV`, install the generated package, and call `GraftConfig.SetConfig` with the same channel settings inside `plugin`. `host` next to `runtime` is the Gateway HTTP address, not the broker.

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

Part of the [Graftcode](https://github.com/grft-dev/graftcode) project.
