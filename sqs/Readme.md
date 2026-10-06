# Amazon SQS Plugin

This plugin carries Graftcode Gateway calls over Amazon Simple Queue Service. It implements `Hypertube::Native::Interfaces::ITransport` and `GraftcodeGateway::IServer`, and exports `CreateTransportChannel` / `DestroyTransportChannel` and `CreateServer` / `DestroyServer`.

It is written in C++ and uses the AWS SDK for C++ (`aws-sdk-cpp[sqs]`) plus `nlohmann-json`, both from vcpkg.

Binary Graft payloads are Base64-encoded in the SQS message body with the prefix `graftcode-base64:`. RPC metadata is message attributes:

- `GraftcodeCorrelationId` identifies one call.
- `GraftcodeReplyTo` is the caller's reply queue URL. The gateway does not use a fixed reply queue; each request names its own.

## Pitfalls

- SQS is at-least-once. The gateway deletes a request only after `processMessage` succeeds and, for RPC, the reply has been sent. A failure leaves the message for the visibility timeout, then another receive. Make called operations idempotent, and attach a dead-letter queue for messages that never succeed.
- SQS cannot filter a receive by message attribute. Give each client process its own reply queue. Do not share one reply queue between independent processes. Calls through one transport instance are serialized.
- Encoded bodies larger than 1 MiB are rejected. Some LocalStack versions and older queues still enforce 256 KiB.
- Queue URLs and the SDK endpoint are different. `endpointOverride` is where the SDK connects (`http://localhost:4566` for LocalStack). `requestQueueUrl` / `replyQueueUrl` must be the URL the broker returns. LocalStack's working form is `http://sqs.<region>.localhost.localstack.cloud:4566/000000000000/<queue>`, not `http://localhost:4566/000000000000/<queue>`.

Standard and FIFO queues are supported. A queue URL ending in `.fifo` gets `MessageGroupId` (default `graftcode`) and a `MessageDeduplicationId`. Request sends reuse the correlation id so an SDK retry does not enqueue a second request. Reply sends use a new id so a redelivered request can still publish a response inside FIFO's deduplication window.

## Build

```bash
git clone https://github.com/grft-dev/graftcode-extensions.git
cd graftcode-extensions/sqs

git clone https://github.com/microsoft/vcpkg.git
./vcpkg/bootstrap-vcpkg.sh   # Windows: .\vcpkg\bootstrap-vcpkg.bat

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=./vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Output:

- Windows: `sqs/build/SqsPlugin/SqsPlugin.dll` — config `"name": "SqsPlugin"`
- Linux/macOS: `sqs/build/SqsPlugin/libSqsPlugin.so` or `.dylib` — config `"name": "libSqsPlugin"`

On Windows, copy `SqsPlugin.dll` and the AWS SDK runtime DLLs next to `gg.exe`. The loader does not search the build tree.

Download `gg` from https://github.com/grft-dev/graftcode-gateway/releases/.

## LocalStack

Sample files and compose live in [`samples/sqs`](../samples/sqs/README.md). From `sqs/` you can also start the same LocalStack; the compose file creates `graft-requests` and `graft-replies`:

```bash
docker compose up -d
```

If the init hook did not run:

```bash
aws --endpoint-url http://localhost:4566 sqs create-queue --queue-name graft-requests --region us-east-1
aws --endpoint-url http://localhost:4566 sqs create-queue --queue-name graft-replies --region us-east-1
aws --endpoint-url http://localhost:4566 sqs get-queue-url --queue-name graft-requests --region us-east-1
```

Use the printed queue URL in config. With region `us-east-1` that is:

- `http://sqs.us-east-1.localhost.localstack.cloud:4566/000000000000/graft-requests`
- `http://sqs.us-east-1.localhost.localstack.cloud:4566/000000000000/graft-replies`

`endpointOverride` stays `http://localhost:4566`. Static `test` / `test` credentials are for LocalStack only.

## Windows smoke test

This layout was run locally against LocalStack. Put these files in one folder:

- `gg.exe`
- the hosted module, for example `netapp.dll`
- `SqsPlugin.dll`
- the AWS SDK runtime DLLs from the plugin build

`sqsplugin.json` (same contents as [`samples/sqs/pluginConfig.gateway.json`](../samples/sqs/pluginConfig.gateway.json)):

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

From that folder:

```powershell
.\gg.exe netapp.dll --config sqsplugin.json
```

Open `http://localhost:81/GV` and install the generated package (the verified client used `graft.nuget.netapp`).

## Client

`host` is the Gateway (`localhost:80`), not SQS. `clientconfig.json` matches [`samples/sqs/graftConfig.sqs.example.json`](../samples/sqs/graftConfig.sqs.example.json):

```csharp
graft.nuget.netapp.GraftConfig.SetConfig("clientconfig.json");
var calculator = new graft.nuget.netapp.Calculator();
calculator.Add(1, 2);
```

```json
{
  "configurations": {
    "graft.nuget.netapp": {
      "runtime": "netcore",
      "host": "localhost:80",
      "stateless": true,
      "plugin": {
        "name": "SqsPlugin",
        "region": "us-east-1",
        "endpointOverride": "http://localhost:4566",
        "accessKeyId": "test",
        "secretAccessKey": "test",
        "requestQueueUrl": "http://sqs.us-east-1.localhost.localstack.cloud:4566/000000000000/graft-requests",
        "replyQueueUrl": "http://sqs.us-east-1.localhost.localstack.cloud:4566/000000000000/graft-replies",
        "rpcTimeoutMs": 30000,
        "waitTimeSeconds": 10,
        "visibilityTimeoutSeconds": 60,
        "verifySsl": false
      }
    }
  }
}
```

On AWS, omit `endpointOverride`, `accessKeyId`, `secretAccessKey`, and `verifySsl`. The SDK default credential chain (IAM role, environment, or shared config) is used. `accessKeyId` and `secretAccessKey` must be set together; `sessionToken` is optional.

The gateway identity needs `sqs:ReceiveMessage`, `sqs:DeleteMessage`, and `sqs:SendMessage` on the request queue (and `sqs:SendMessage` on reply queues). A client needs `sqs:SendMessage` on the request queue and `sqs:ReceiveMessage` plus `sqs:DeleteMessage` on its reply queue.

## One-way calls

Set `"oneWay": true` on both sides. The client omits `GraftcodeReplyTo` and does not wait. The gateway processes the request and deletes it without sending a reply. See [`samples/sqs/pluginConfig.oneway.json`](../samples/sqs/pluginConfig.oneway.json). A reply queue is not required.

## Configuration reference

| Field | Required | Description |
|-------|----------|-------------|
| `name` | yes | Plugin library name without extension. |
| `region` | no | AWS region. Default `us-east-1`. |
| `requestQueueUrl` | yes | Request queue URL. Aliases: `queueUrl`, `queue`. |
| `replyQueueUrl` | RPC client | This process's reply queue URL. Alias: `replyQueue`. Not required on the gateway. |
| `rpcTimeoutMs` | no | How long the client waits for a reply. Default `30000`. |
| `waitTimeSeconds` | no | Long-poll duration, 1–20. Default `1`. |
| `visibilityTimeoutSeconds` | no | Per-receive visibility timeout, 1–43200. Default `30`. Set it above the longest call. |
| `oneWay` | no | Skip the reply. Default `false`. |
| `messageGroupId` | no | FIFO group id, max 128 characters. Default `graftcode`. |
| `endpointOverride` | no | SDK endpoint. `http://` selects plain HTTP and strips the scheme before the SDK sees the host. |
| `verifySsl` | no | TLS verification. Default `true`. Set `false` for LocalStack HTTP. |
| `accessKeyId`, `secretAccessKey`, `sessionToken` | no | Explicit credentials. Otherwise the AWS default chain is used. |

Settings may be a flat object, as in the samples, or nested under an `sqs` object.
