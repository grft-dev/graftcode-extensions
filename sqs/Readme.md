# Amazon SQS Plugin

This native C++ plugin carries Graftcode Gateway calls over Amazon Simple Queue
Service (SQS). It implements the standard Graftcode transport and server
interfaces and exports `CreateTransportChannel` / `DestroyTransportChannel` and
`CreateServer` / `DestroyServer`.

Binary Graftcode payloads are Base64-encoded into the SQS message body. RPC
metadata is carried in message attributes:

- `GraftcodeCorrelationId` identifies the request and response.
- `GraftcodeReplyTo` contains the caller's reply queue URL.

## Delivery model

SQS provides at-least-once delivery. The server deletes a request only after
`processMessage` succeeds and, for RPC, the response has been sent. Configure a
dead-letter queue and make called operations idempotent.

SQS cannot filter receives by message attribute. Consequently, an RPC reply
queue must be dedicated to one client/runtime plugin instance. Do not share one
reply queue between independent processes. Calls made through one transport
instance are serialized.

Standard and FIFO queues are supported. For a FIFO queue URL ending in
`.fifo`, the plugin automatically sets `MessageGroupId` and
`MessageDeduplicationId`.

## Build

The plugin uses the AWS SDK for C++ (`sqs`) and `nlohmann-json`, installed in
vcpkg manifest mode.

```powershell
git clone https://github.com/microsoft/vcpkg.git
.\vcpkg\bootstrap-vcpkg.bat

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_TOOLCHAIN_FILE=.\vcpkg\scripts\buildsystems\vcpkg.cmake
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

The output is `build/SqsPlugin/SqsPlugin.dll` on Windows or
`build/SqsPlugin/libSqsPlugin.so` / `.dylib` on Linux/macOS. Use
`libSqsPlugin` as the configured plugin name when the library has the `lib`
prefix.

## Create queues

Create one request queue and one reply queue per client runtime:

```bash
aws sqs create-queue --queue-name graft-requests
aws sqs create-queue --queue-name graft-replies-client-1
```

Use the returned queue URLs in configuration. The gateway identity needs
`sqs:ReceiveMessage`, `sqs:DeleteMessage`, and `sqs:SendMessage`; a client needs
`sqs:SendMessage`, `sqs:ReceiveMessage`, and `sqs:DeleteMessage` for its queues.

## Gateway configuration

```json
{
  "name": "SqsPlugin",
  "region": "eu-central-1",
  "requestQueueUrl": "https://sqs.eu-central-1.amazonaws.com/123456789012/graft-requests",
  "waitTimeSeconds": 10,
  "visibilityTimeoutSeconds": 60
}
```

Run the Gateway:

```powershell
./gg .\PhysicsCalculator.dll --config .\pluginConfig.json
```

## Runtime configuration

```csharp
string configSource =
"""
{
  "configurations": {
    "graft.nuget.PhysicsCalculator": {
      "runtime": "netcore",
      "stateless": true,
      "plugin": {
        "name": "SqsPlugin",
        "region": "eu-central-1",
        "requestQueueUrl": "https://sqs.eu-central-1.amazonaws.com/123456789012/graft-requests",
        "replyQueueUrl": "https://sqs.eu-central-1.amazonaws.com/123456789012/graft-replies-client-1",
        "rpcTimeoutMs": 30000,
        "waitTimeSeconds": 10
      }
    }
  }
}
""";

graft.nuget.PhysicsCalculator.GraftConfig.SetConfig(configSource);
```

The AWS SDK default credential provider chain is used. Prefer IAM roles,
environment variables, shared AWS config, or workload identity. Static
credentials are accepted as `accessKeyId`, `secretAccessKey`, and optional
`sessionToken`, primarily for local emulators.

## One-way calls

Set `"oneWay": true` in both runtime and gateway configuration. The client sends
without `GraftcodeReplyTo`, and the gateway processes and deletes the request
without sending a response.

## LocalStack

Start the included LocalStack setup:

```bash
docker compose up -d
aws --endpoint-url http://localhost:4566 sqs create-queue --queue-name graft-requests
aws --endpoint-url http://localhost:4566 sqs create-queue --queue-name graft-replies
```

Use this additional configuration:

```json
{
  "region": "us-east-1",
  "endpointOverride": "http://localhost:4566",
  "accessKeyId": "test",
  "secretAccessKey": "test",
  "verifySsl": false
}
```

## Configuration reference

- `region` — AWS region; defaults to `us-east-1`.
- `requestQueueUrl` — required request queue URL (`queueUrl` and `queue` aliases
  are accepted).
- `replyQueueUrl` — required by an RPC client; optional on the gateway because
  each request supplies its reply destination.
- `rpcTimeoutMs` — RPC response timeout; defaults to `30000`.
- `waitTimeSeconds` — SQS long-poll duration, 1–20; defaults to `1`.
- `visibilityTimeoutSeconds` — per-receive visibility timeout, 1–43200;
  defaults to `30`. Set it longer than the maximum call execution time.
- `oneWay` — process without a response; defaults to `false`.
- `messageGroupId` — FIFO message group; defaults to `graftcode`.
- `endpointOverride` — custom SQS endpoint, such as LocalStack.
- `verifySsl` — TLS certificate verification; defaults to `true`.
- `accessKeyId`, `secretAccessKey`, `sessionToken` — optional explicit
  credentials; otherwise the AWS SDK provider chain is used.
