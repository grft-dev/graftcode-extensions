# Kafka Plugin

This plugin carries Graftcode Gateway calls over Apache Kafka. It implements `Hypertube::Native::Interfaces::ITransport` and `GraftcodeGateway::IServer`, and exports `CreateTransportChannel` / `DestroyTransportChannel` and `CreateServer` / `DestroyServer`.

It is written in C++. CMake FetchContent builds nlohmann/json `v3.12.0`, librdkafka `v2.8.0` (static `rdkafka++`), zlib `v1.3.1`, and zstd `v1.5.6`, all linked into the plugin. OpenSSL is static too: on Windows configure with the vcpkg toolchain so it is the `/MT` build from `kafka/vcpkg.json` (the OpenSSL in `Program Files` is `/MD`); on Linux and macOS the build uses the PIC static archive from the system or Homebrew. LZ4 and Snappy are the copies compiled into librdkafka. The first configure needs network access.

Windows and macOS release archives contain only `KafkaPlugin.dll` or `libKafkaPlugin.dylib`. The Linux archive contains `libKafkaPlugin.so` plus `libsasl2.so.2` and `libsasl2.so.2.0.25`. Cyrus SASL loads mechanism modules at runtime, so `libsasl2` stays shared; copy those two files next to the plugin. The plugin's own TLS uses the static OpenSSL. On Windows the MSVC CRT is static (`/MT`), so the VC++ Redistributable is not required. Clients do not install OpenSSL, zlib, zstd, or librdkafka separately.

Kafka has no native request/reply. The plugin uses headers:

1. The client produces to `requestTopic` with `correlation-id` and `reply-to`.
2. The gateway consumes `requestTopic`, runs `processMessage`, and produces the response to `reply-to` (or the configured `replyTopic` when the header is absent), echoing `correlation-id`.
3. The client consumes its reply topic until that correlation id arrives, or `rpcTimeoutMs` elapses.

Each client appends its own instance id to `groupId`, so several clients can share one reply topic. Each consumer group receives the replies and keeps only its correlation id. The gateway uses `groupId` as given (default `graft-gateway`). There is no one-way mode.

`brokers` defaults to `localhost:9092`, `requestTopic` to `graft.requests`, `replyTopic` to `graft.replies`, and `rpcTimeoutMs` to `30000`. `queue` is an alias of `requestTopic`, `replyQueue` of `replyTopic`, and `host` of `brokers`.

## Build

```bash
git clone https://github.com/grft-dev/graftcode-extensions.git
cd graftcode-extensions/kafka

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
# Windows: add -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release
```

Output:

- Windows: `kafka/build/KafkaPlugin/KafkaPlugin.dll` — config `"name": "KafkaPlugin"`
- Linux/macOS: `kafka/build/KafkaPlugin/libKafkaPlugin.so` or `.dylib` — config `"name": "libKafkaPlugin"`

Download `gg` from https://github.com/grft-dev/graftcode-gateway/releases/.

## Local broker

Compose files are in [`samples/kafka`](../samples/kafka/README.md), not in this directory.

Apache Kafka (KRaft), broker `localhost:9092`, UI `http://localhost:8080`:

```bash
cd ../samples/kafka
docker compose up -d
./scripts/create-topics.sh
```

Redpanda, broker `localhost:19092`:

```bash
docker compose -f docker-compose.redpanda.yml up -d
```

Set `"brokers": "localhost:19092"` when using Redpanda.

## Gateway

`pluginConfig.json` (also [`samples/kafka/pluginConfig.gateway.json`](../samples/kafka/pluginConfig.gateway.json)):

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

```powershell
./gg .\PhysicsCalculator.dll --config .\pluginConfig.json
```

Open `http://localhost:81/GV` and install the generated package.

## Client

A full example is [`samples/kafka/graftConfig.kafka.example.json`](../samples/kafka/graftConfig.kafka.example.json). Use a client `groupId` such as `graft-client`; the plugin still adds a unique suffix. Kafka bootstrap servers belong in `plugin.brokers`. When the runtime also needs the Gateway HTTP address, set `"host": "localhost:80"` next to `runtime`, as in the [SQS client config](../samples/sqs/graftConfig.sqs.example.json).

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
| `name` | yes | Plugin library name without extension. |
| `brokers` / `host` | no | Bootstrap servers. Default `localhost:9092`. |
| `requestTopic` / `queue` | no | Request topic. Default `graft.requests`. |
| `replyTopic` / `replyQueue` | no | Reply topic. Default `graft.replies`. |
| `groupId` | no | Gateway consumer group, or the prefix of each client's unique group. |
| `rpcTimeoutMs` | no | Client wait for a matching reply. Default `30000`. |
| `securityProtocol` | no | librdkafka `security.protocol`, for example `SASL_SSL` or `SSL`. |
| `saslMechanism` | no | For example `PLAIN` or `SCRAM-SHA-512`. |
| `saslUsername`, `saslPassword` | no | SASL credentials. |
| `sslCaLocation` | no | CA bundle path for TLS. |

Empty security fields leave librdkafka defaults (plaintext for the local compose files).
