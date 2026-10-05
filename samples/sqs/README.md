# SQS sample configs

Local Amazon SQS (LocalStack) and Graftcode plugin connection files.

## Start LocalStack

```bash
cd samples/sqs
docker compose up -d
```

- SQS endpoint: `http://localhost:4566`
- Account id used by LocalStack queue URLs: `000000000000`

`docker compose up` creates `graft-requests` and `graft-replies`. To create them
again from the host:

```bash
./scripts/create-queues.sh
```

Queue URLs:

- `http://localhost:4566/000000000000/graft-requests`
- `http://localhost:4566/000000000000/graft-replies`

Give each runtime its own reply queue. SQS cannot filter a receive by
correlation id, so independent clients must not share one reply queue.

## Config files

| File | Use |
|------|-----|
| `pluginConfig.gateway.json` | `gg YourModule.dll --config pluginConfig.gateway.json` |
| `pluginConfig.client.json` | Client-side plugin block |
| `pluginConfig.oneway.json` | Fire-and-forget (`oneWay: true`) for both sides |
| `graftConfig.sqs.example.json` | Full GraftConfig example for .NET |
| `pluginConfig.json` | Same as gateway (default name) |

Static `test` / `test` credentials are for LocalStack only. On AWS, omit
`endpointOverride`, `accessKeyId`, `secretAccessKey`, and `verifySsl`, and let
the AWS SDK default credential chain resolve an IAM role or environment
credentials. See `sqs/Readme.md` for the full field list.

If the built library is named `libSqsPlugin.so` or `libSqsPlugin.dylib`, set
`"name": "libSqsPlugin"`.
