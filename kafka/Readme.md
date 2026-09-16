# Kafka Plugin Build (CMake)

This plugin is the Apache Kafka counterpart of the RabbitMQ / Azure Service Bus
plugins. It implements the same Graftcode plugin interfaces
(`Hypertube::Native::Interfaces::ITransport` for the calling runtime and
`GraftcodeGateway::IServer` for the gateway) and exposes the same exported
factory symbols (`CreateTransportChannel` / `DestroyTransportChannel` and
`CreateServer` / `DestroyServer`).

It is written in C++ and talks to Kafka using **librdkafka** (`rdkafka++`),
acquired either through CMake `FetchContent` (default) or vcpkg / a system
install.

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

Each client instance appends a unique suffix to `groupId` so concurrent clients
sharing one reply topic each receive a copy of replies and filter by
`correlation-id`. For higher fan-out efficiency you can give each client its own
`replyTopic` (and set it in config); the server always honors `reply-to`.

## 1) Place this folder in graftcode-extensions

Copy the contents of this directory to `kafka/` inside
[graftcode-extensions](https://github.com/grft-dev/graftcode-extensions)
(alongside `rabbitmq/` and `servicebus/`).

```bash
cd graftcode-extensions/kafka
```

## 2) Configure with CMake (FetchContent — default)

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

This downloads **nlohmann/json** and **librdkafka** (`KAFKA_LIBRDKAFKA_GIT_TAG`,
default `v2.8.0`) via FetchContent and links a static `rdkafka++` into the
shared plugin.

Optional flags:

| Flag | Meaning |
|------|---------|
| `-DKAFKA_LIBRDKAFKA_GIT_TAG=v2.15.0` | Pin a different librdkafka release |
| `-DKAFKA_USE_SYSTEM_RDKAFKA=ON` | Do not FetchContent; require `find_package(RdKafka)` |

### Alternative: vcpkg

```bash
git clone https://github.com/microsoft/vcpkg.git
./vcpkg/bootstrap-vcpkg.sh   # Windows: .\vcpkg\bootstrap-vcpkg.bat

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=./vcpkg/scripts/buildsystems/vcpkg.cmake \
  -DKAFKA_USE_SYSTEM_RDKAFKA=ON
cmake --build build --config Release
```

Manifest dependencies are declared in `vcpkg.json` (`librdkafka`, `nlohmann-json`).

## 3) Build output

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
docker compose up -d
./scripts/create-topics.sh
# or: KAFKA_CONTAINER=<name> ./scripts/create-topics.sh
```

Brokers: `localhost:9092`.

### Redpanda

```bash
docker compose -f docker-compose.redpanda.yml up -d
# topics (example):
docker exec -it <redpanda-container> rpk topic create graft.requests graft.replies
```

Brokers: `localhost:19092` — set `"brokers": "localhost:19092"` in config.

## 6) Run GG with a sample library

Create `pluginConfig.json` next to your module (a ready sample lives in this
folder):

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

If you built `libKafkaPlugin.*`, set `"name": "libKafkaPlugin"`.

```bash
./gg ./YourLibrary.dll --config ./pluginConfig.json
```

## 7) Get installation command

Visit `http://localhost:81/GV`, select your package manager, and copy the
generated installation command.

## 8) Configure Graft after installation

```csharp
string configSource =
"""
{
  "configurations": {
    "graft.nuget.PhysicsCalculator": {
      "runtime": "netcore",
      "stateless": true,
      "plugin": {
        "name": "KafkaPlugin",
        "brokers": "localhost:9092",
        "requestTopic": "graft.requests",
        "replyTopic": "graft.replies",
        "groupId": "graft-client",
        "rpcTimeoutMs": 30000
      }
    }
  }
}
""";

graft.nuget.PhysicsCalculator.GraftConfig.SetConfig(configSource);
```

## Configuration reference

| Field | Required | Description |
|-------|----------|-------------|
| `brokers` | yes* | Kafka bootstrap servers (`host:port[,host:port…]`). |
| `host` | * | Alias for `brokers` (compatibility with other plugins). |
| `requestTopic` | yes | Topic requests are produced to / consumed from. |
| `queue` | | Alias for `requestTopic`. |
| `replyTopic` | yes (client) | Topic replies are consumed from / default produce target. |
| `replyQueue` | | Alias for `replyTopic`. |
| `groupId` | no | Consumer group. Server default `graft-gateway`. Client default `graft-client` (a unique instance suffix is always appended on the client). |
| `rpcTimeoutMs` | no | Client request/response timeout in milliseconds (default `30000`). |
| `securityProtocol` | no | librdkafka `security.protocol` (e.g. `SASL_SSL`, `SSL`, `PLAINTEXT`). |
| `saslMechanism` | no | e.g. `PLAIN`, `SCRAM-SHA-256`, `SCRAM-SHA-512`. |
| `saslUsername` | no | SASL username. |
| `saslPassword` | no | SASL password. |
| `sslCaLocation` | no | Path to CA PEM for SSL verification. |

\* Provide `brokers` or `host`.

## Smoke test

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build --output-on-failure
```

`KafkaPluginTest` links the shared library and exercises
`CreateServer` / `CreateTransportChannel` without requiring a broker.
End-to-end RPC still needs a running Kafka/Redpanda and the topics above.

## Layout (drop-in as `kafka/`)

```
kafka/
  CMakeLists.txt
  vcpkg.json
  pluginConfig.json
  docker-compose.yml
  docker-compose.redpanda.yml
  Readme.md
  BUILD_NOTES.md
  GraftcodePluginsInterfaces/
  KafkaPlugin/
  KafkaPluginTest/
  scripts/create-topics.sh
```
