# Google Cloud Pub/Sub plugin

This plugin carries Graftcode Gateway calls over Google Cloud Pub/Sub. It implements `Hypertube::Native::Interfaces::ITransport` and `GraftcodeGateway::IServer`, and exports `CreateTransportChannel` / `DestroyTransportChannel` and `CreateServer` / `DestroyServer`.

It is written in C++. Dependencies are libcurl, OpenSSL, and nlohmann/json, installed with the same vcpkg toolchain file as the SQS and Service Bus plugins.

## Why not google-cloud-cpp

`google-cloud-cpp`'s Pub/Sub client is a CMake superbuild over gRPC, protobuf, and Abseil. That build is large enough to exhaust memory on the six-platform release matrix and is fragile to reproduce. This plugin calls the [Pub/Sub JSON REST API](https://cloud.google.com/pubsub/docs/reference/rest) (`topics.publish`, `subscriptions.pull`, `subscriptions.acknowledge`, and topic/subscription create) with libcurl. OpenSSL signs the service-account JWT used for production OAuth. The local emulator and production share one code path.

See [BUILD_NOTES.md](BUILD_NOTES.md).

## Reply model

Pub/Sub has topics and subscriptions, not queues. A publish to a topic is delivered to every subscription that already exists on that topic. This plugin maps Graft RPC onto that as follows:

1. The client publishes the request to `requestTopic`. Message attributes are `correlationId` (one id per call) and `replyTo` (the client's reply **topic** id).
2. The gateway consumes `requestSubscription` on that topic, runs `processMessage`, and publishes the response to the `replyTo` topic with the same `correlationId`.
3. The client pulls `replySubscription` (its subscription on `replyTopic`) until a message with that `correlationId` arrives, or `rpcTimeoutMs` elapses.

`replyTo` is a topic id, not a subscription. The client creates or uses a subscription on that topic before it publishes, so the reply is not lost. Give each client process its own reply topic and reply subscription. The client acks every pulled reply, including ones whose correlation id does not match, so a dedicated subscription does not fill up with timed-out calls. Do not share one reply subscription between processes.

When `requestSubscription` or `replySubscription` is omitted, the plugin appends `-sub` to the topic name (`graft-requests` becomes `graft-requests-sub`).

## Delivery and ack

Pub/Sub is at-least-once. The gateway acks a request only after `processMessage` succeeds and, for RPC, the reply has been published. A failure leaves the message unacked. Pub/Sub redelivers it after `ackDeadlineSeconds` (10–600). Set that above the longest call.

A request that is missing `correlationId` or a valid `replyTo` is also left unacked, so a dead-letter policy on the request subscription can take over. Configure `deadLetterPolicy` and `retryPolicy` on the subscription in Google Cloud when a poison message should stop being retried. The plugin does not change those policies. Make called operations idempotent: a crash after the reply is published and before the ack produces a second reply. The client drops replies whose correlation id does not match.

Pulls use the REST `:pull` method, which returns immediately when the subscription is empty. The client polls until `rpcTimeoutMs`. Calls through one transport instance are serialized.

Decoded payloads larger than 10 MiB are rejected. The JSON API carries message data as standard base64; that encoding is part of the Pub/Sub protocol, not an extra wrapper.

## Build

```bash
git clone https://github.com/grft-dev/graftcode-extensions.git
cd graftcode-extensions/pubsub

git clone https://github.com/microsoft/vcpkg.git
./vcpkg/bootstrap-vcpkg.sh   # Windows: .\vcpkg\bootstrap-vcpkg.bat

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=./vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Output:

- Windows: `pubsub/build/PubSubPlugin/PubSubPlugin.dll` — config `"name": "PubSubPlugin"`
- Linux/macOS: `pubsub/build/PubSubPlugin/libPubSubPlugin.so` or `.dylib` — config `"name": "libPubSubPlugin"`

On Windows, copy `PubSubPlugin.dll` next to `gg.exe`. curl, OpenSSL, zlib, and the VC++ runtime are linked into that DLL (`/MT` and the `*-windows-static` vcpkg triplet). curl uses Schannel. No VC++ Redistributable install is required.

Hermetic tests always run. The round-trip test runs only when `PUBSUB_EMULATOR_HOST` or `PUBSUB_LIVE` is set.

Download `gg` from https://github.com/grft-dev/graftcode-gateway/releases/.

## Emulator

Sample files and compose live in [`samples/pubsub`](../samples/pubsub/README.md). From `pubsub/` you can start the same emulator:

```bash
docker compose up -d
```

The emulator listens on `localhost:8085`. It does not create topics. Sample configs set `autoCreate` to true, so the plugin creates `graft-requests`, `graft-requests-sub`, `graft-replies`, and `graft-replies-sub` in project `graftcode-local` when the gateway and the client start. To create them yourself:

```bash
./../samples/pubsub/scripts/create-resources.sh
```

`PUBSUB_EMULATOR_HOST` (for example `localhost:8085`) is used when `emulatorHost` is omitted. No credentials are sent to the emulator.

## Windows smoke test

This layout is the same shape as the SQS sample. Put these files in one folder:

- `gg.exe`
- the hosted module, for example `netapp.dll`
- `PubSubPlugin.dll`
- `PubSubPlugin.dll` only. curl, libcrypto, and zlib are inside the DLL.

`pubsubplugin.json` (same contents as [`samples/pubsub/pluginConfig.gateway.json`](../samples/pubsub/pluginConfig.gateway.json)):

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

From that folder, with the emulator running:

```powershell
.\gg.exe netapp.dll --config pubsubplugin.json
```

Open `http://localhost:81/GV` and install the generated package.

## Client

`host` is the Gateway (`localhost:80`), not Pub/Sub. `clientconfig.json` matches [`samples/pubsub/graftConfig.pubsub.example.json`](../samples/pubsub/graftConfig.pubsub.example.json):

```csharp
graft.nuget.netapp.GraftConfig.SetConfig("clientconfig.json");
var calculator = new graft.nuget.netapp.Calculator();
calculator.Add(1, 2);
```

On Google Cloud, omit `emulatorHost` and `verifySsl`. Provide credentials with one of:

- `accessToken`, or `PUBSUB_ACCESS_TOKEN`
- `credentialsFile`, or `GOOGLE_APPLICATION_CREDENTIALS`, pointing at a service-account JSON key
- the GCE metadata server, when neither of the above is set

The gateway identity needs `pubsub.subscriptions.consume` on the request subscription and `pubsub.topics.publish` on reply topics. A client needs `pubsub.topics.publish` on the request topic and `pubsub.subscriptions.consume` on its reply subscription. `autoCreate` also needs permission to create topics and subscriptions. Set `autoCreate` to false in production and create the resources ahead of time when the runtime identity should not administer Pub/Sub.

## One-way calls

Set `"oneWay": true` on both sides. The client omits `replyTo` and does not wait. The gateway processes the request and acks it without publishing a reply. A reply topic is not required. See [`samples/pubsub/pluginConfig.oneway.json`](../samples/pubsub/pluginConfig.oneway.json).

## Configuration reference

| Field | Required | Description |
|-------|----------|-------------|
| `name` | yes | Plugin library name without extension. |
| `projectId` | yes | GCP project id, or any id the emulator accepts (sample: `graftcode-local`). |
| `requestTopic` | yes | Topic the client publishes requests to. Alias: `queue`. |
| `requestSubscription` | server | Subscription the gateway pulls. Alias: `subscription`. Default: `<requestTopic>-sub`. |
| `replyTopic` | RPC client | This process's reply topic. Alias: `replyQueue`. The gateway reads the per-call `replyTo` attribute instead. |
| `replySubscription` | RPC client | Subscription on `replyTopic`. Default: `<replyTopic>-sub`. One per client process. |
| `rpcTimeoutMs` | no | How long the client waits for a reply. Default `30000`. Max `3600000`. |
| `ackDeadlineSeconds` | no | Subscription ack deadline, 10–600. Default `60`. Set it above the longest call. |
| `maxMessages` | no | Client pull batch size, 1–100. Default `10`. |
| `oneWay` | no | Skip the reply. Default `false`. |
| `autoCreate` | no | Create the topic and subscription if they are missing. Default `true` when an emulator host is set, otherwise `false`. |
| `emulatorHost` | no | `host:port` or a URL. `http://` is plain HTTP. `PUBSUB_EMULATOR_HOST` fills this in when it is empty. |
| `verifySsl` | no | TLS verification for Cloud Pub/Sub and the OAuth token endpoint. Default `true`. |
| `credentialsFile` | no | Service-account JSON key. `GOOGLE_APPLICATION_CREDENTIALS` is used when this is empty. |
| `accessToken` | no | Bearer token. Wins over the credentials file. `PUBSUB_ACCESS_TOKEN` is used when this is empty. |

Topic and subscription ids are 3–255 characters, start with a letter, and contain only letters, digits, `-`, `_`, or `.`.

Settings may be a flat object, as in the samples, or nested under a `pubsub` object.
