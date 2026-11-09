# Local Docker samples

These samples run each messaging service locally together with three Graftcode
Gateway (`gg`) servers consuming from the same request queue or topic.

Available samples:

- `servicebus` - Azure Service Bus emulator on port `5672`; GG on `10301`
- `rabbitmq` - RabbitMQ on port `5673`, management UI on `15672`; GG on `10302`
- `kafka` - Kafka on port `9092`, Kafka UI on `8080`; GG on `10303`
- `sqs` - LocalStack SQS on port `4566`; GG on `10304`
- `pubsub` - Google Pub/Sub emulator on port `8085`; GG on `10305`

The corresponding Graft Vision ports are `10401` through `10405`.

## Prerequisites

- Docker Desktop with Docker Compose
- Internet access during the first image build

The server image downloads the latest Linux Graftcode Gateway and the latest
matching plugin release. Each sample includes `server/netapp.dll` as the hosted
test module.

## Run a sample

Open PowerShell in a sample directory and run:

```powershell
.\run.ps1
```

This executes:

```powershell
docker compose up -d --build
```

Only the first GG server publishes its WebSocket and Vision ports to the host.
The other two servers are internal consumers, avoiding host-port conflicts
while demonstrating competing consumers.

Stop a sample with:

```powershell
docker compose down
```

Use `docker compose down -v` for SQS if you also want to remove persisted
LocalStack data.

## Configuration

The gateway-side plugin configuration is in `server/pluginconfig.json`.
Client applications must use the matching broker address, request destination,
reply destination, and credentials.

`kafka-init` and `sqs-init` are one-shot initialization services. Their expected
state after startup is `Exited (0)`: Kafka topics or SQS queues have been
created successfully.
