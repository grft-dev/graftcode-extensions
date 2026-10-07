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

## Use a plugin

1. Build the plugin (see its Readme). The artifact is a shared library: `SqsPlugin.dll` on Windows, or `libSqsPlugin.so` / `.dylib` on Linux and macOS.
2. Put `gg`, the hosted module, and the plugin library in one folder, plus any extra shared libraries from that plugin's release archive. Windows archives are the plugin DLL alone (static VC++ runtime, no VC++ Redistributable). Linux Kafka still ships `libsasl2` (and the `libcrypto` it imports), plus `libz` and `libzstd`. `name` in the config is the library file name without the extension (`libSqsPlugin` when the file is `libSqsPlugin.so`).
3. Start the Gateway with `--config`. A verified SQS layout is in [samples/sqs](samples/sqs/README.md): `gg netapp.dll --config sqsplugin.json`.
4. Open `http://localhost:81/GV`, install the generated package, and call `GraftConfig.SetConfig` with the same channel settings. `host` there is the Gateway HTTP address, not the broker.

How the Gateway loads a plugin is described in the "Plugin server config" section of the [Graftcode Gateway](https://github.com/grft-dev/graftcode-gateway) README.

Part of the [Graftcode](https://github.com/grft-dev/graftcode) project.
