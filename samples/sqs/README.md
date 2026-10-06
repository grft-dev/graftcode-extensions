# SQS sample configs

Local Amazon SQS (LocalStack) and the Graftcode plugin files used for a working Windows smoke test.

## Start LocalStack

```bash
cd samples/sqs
docker compose up -d
```

- SDK endpoint (`endpointOverride`): `http://localhost:4566`
- Region: `us-east-1`
- Account id in LocalStack queue URLs: `000000000000`

`docker compose up` runs `scripts/init-queues.sh` and creates `graft-requests` and `graft-replies`. To create them again from the host:

```bash
./scripts/create-queues.sh
```

The AWS CLI talks to `http://localhost:4566`. Put the queue URL that `get-queue-url` prints into the plugin config. LocalStack returns:

- `http://sqs.us-east-1.localhost.localstack.cloud:4566/000000000000/graft-requests`
- `http://sqs.us-east-1.localhost.localstack.cloud:4566/000000000000/graft-replies`

`localhost.localstack.cloud` resolves to 127.0.0.1. Do not replace that host with `localhost` in the queue URL. The SDK endpoint and the queue URL are different settings.

Give each runtime its own reply queue. SQS cannot filter a receive by correlation id, so independent clients must not share one reply queue. Delivery is at-least-once; pair the request queue with a dead-letter queue when a failed call should stop being retried. Field list: [sqs/Readme.md](../../sqs/Readme.md).

## Windows smoke test

Colocate these next to each other:

- `gg.exe`
- `netapp.dll` (the hosted module)
- `SqsPlugin.dll`
- the AWS SDK runtime DLLs

Copy `pluginConfig.gateway.json` to that folder as `sqsplugin.json` and run:

```powershell
.\gg.exe netapp.dll --config sqsplugin.json
```

Install `graft.nuget.netapp` from `http://localhost:81/GV`. In the client project:

```csharp
graft.nuget.netapp.GraftConfig.SetConfig("clientconfig.json");
var calculator = new graft.nuget.netapp.Calculator();
calculator.Add(1, 2);
```

`clientconfig.json` is `graftConfig.sqs.example.json`. `host` is `localhost:80` (the Gateway). `endpointOverride` stays `http://localhost:4566`.

Static `test` / `test` credentials are for LocalStack only. On AWS, omit `endpointOverride`, `accessKeyId`, `secretAccessKey`, and `verifySsl`.

If the built library is `libSqsPlugin.so` or `libSqsPlugin.dylib`, set `"name": "libSqsPlugin"`.

## Config files

| File | Use |
|------|-----|
| `pluginConfig.gateway.json` | Gateway config. Save as `sqsplugin.json` next to `gg.exe`. |
| `pluginConfig.json` | Same as the gateway file. |
| `pluginConfig.client.json` | Plugin object only (no `configurations` wrapper). |
| `graftConfig.sqs.example.json` | Client `GraftConfig` JSON. Save as `clientconfig.json`. |
| `pluginConfig.oneway.json` | Fire-and-forget (`oneWay: true`) for both sides. No reply queue. |
