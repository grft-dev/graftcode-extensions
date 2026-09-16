# Kafka Plugin Build (CMake)

This plugin is the Apache Kafka counterpart of the RabbitMQ / Azure Service Bus
plugins. It implements the same Graftcode plugin interfaces
(`Hypertube::Native::Interfaces::ITransport` for the calling runtime and
`GraftcodeGateway::IServer` for the gateway) and exposes the same exported
factory symbols (`CreateTransportChannel` / `DestroyTransportChannel` and
`CreateServer` / `DestroyServer`).

It is written in C++ and talks to Kafka using **librdkafka** (`rdkafka++`),
acquired through CMake `FetchContent` (same pattern as the RabbitMQ plugin).

## RPC model

Kafka has no native request/reply. This plugin mirrors the Service Bus / AMQP
pattern with message headers:

1. **Client** produces to `requestTopic` with headers:
   - `correlation-id` — UUID per call
   - `reply-to` — reply topic the server should use
2. **Server** (GG plugin) consumes `requestTopic`, runs `processMessage`, then
   produces to the `reply-to` topic (falling back to configured `replyTopic`)
   echoing the same `correlation-id`.
3. **Client** consumes `replyTopic` until a message with a matching
   `correlation-id` arrives, or `rpcTimeoutMs` elapses.

## 1) Clone repository

```bash
git clone https://github.com/grft-dev/graftcode-extensions.git
cd graftcode-extensions/kafka
```

## 2) Configure with CMake

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
```

## 3) Build

```bash
cmake --build build --config Release
```

CMake downloads **nlohmann/json** and **librdkafka** (`v2.8.0`) via FetchContent
and links a static `rdkafka++` into the shared plugin.

As a result, you will receive:
- `kafka/build/KafkaPlugin/KafkaPlugin.dll` (Windows)
- `kafka/build/KafkaPlugin/libKafkaPlugin.so` / `.dylib` (Linux / macOS)

If the generated library is `libKafkaPlugin.*`, use plugin name
`libKafkaPlugin` in config.

## 4) Download GG

Download `gg` from:
https://github.com/grft-dev/graftcode-gateway/releases/

## 5) Run a local broker

### Apache Kafka (KRaft)

```bash
docker compose -f docker-compose.yml up -d
./scripts/create-topics.sh
```

### Redpanda (lighter alternative)

```bash
docker compose -f docker-compose.redpanda.yml up -d
# brokers: localhost:19092
```

## 6) Run GG with sample library

Create `pluginConfig.json` (see also the example in this folder):

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

Then run:

```powershell
./gg .\PhysicsCalculator.dll --config .\pluginConfig.json
```

## Configuration reference

| Field | Required | Description |
|-------|----------|-------------|
| `brokers` / `host` | yes | Kafka bootstrap servers |
| `requestTopic` / `queue` | yes | Topic for requests |
| `replyTopic` / `replyQueue` | yes (RPC client) | Topic for replies |
| `groupId` | no | Consumer group base name |
| `rpcTimeoutMs` | no | Request/response timeout (default 30000) |
