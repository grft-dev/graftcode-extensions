# Pub/Sub sample configs

Local Google Cloud Pub/Sub emulator and the Graftcode plugin files used for a Windows smoke test.

## Start the emulator

```bash
cd samples/pubsub
docker compose up -d
```

- Emulator (`emulatorHost` / `PUBSUB_EMULATOR_HOST`): `localhost:8085`
- Project id: `graftcode-local`
- Request topic / subscription: `graft-requests` / `graft-requests-sub`
- Reply topic / subscription: `graft-replies` / `graft-replies-sub`

The emulator does not create topics on startup. Sample configs set `"autoCreate": true`, so the plugin creates those four resources when `gg.exe` and the client start. To create them from the host instead:

```bash
./scripts/create-resources.sh
```

`create-resources.sh` talks to `http://localhost:8085`. Give each runtime its own reply topic and subscription. Pub/Sub delivers every message on a topic to every subscription, and this plugin acks replies that do not match the in-flight correlation id. Delivery is at-least-once; set a dead-letter policy on `graft-requests-sub` when a failed call should stop being retried. Field list: [pubsub/Readme.md](../../pubsub/Readme.md).

## Windows smoke test

Colocate these next to each other:

- `gg.exe`
- `netapp.dll` (the hosted module)
- `PubSubPlugin.dll`
- the libcurl and OpenSSL runtime DLLs from `pubsub/build/vcpkg_installed/<triplet>/bin` (typically `libcurl.dll`, `libssl-3-x64.dll`, `libcrypto-3-x64.dll`, and `zlib1.dll`)

Copy `pluginConfig.gateway.json` to that folder as `pubsubplugin.json` and run:

```powershell
.\gg.exe netapp.dll --config pubsubplugin.json
```

Install `graft.nuget.netapp` from `http://localhost:81/GV`. In the client project:

```csharp
graft.nuget.netapp.GraftConfig.SetConfig("clientconfig.json");
var calculator = new graft.nuget.netapp.Calculator();
calculator.Add(1, 2);
```

`clientconfig.json` is `graftConfig.pubsub.example.json`. `host` is `localhost:80` (the Gateway). `emulatorHost` stays `localhost:8085`.

No credentials are required for the emulator. On Google Cloud, omit `emulatorHost` and `verifySsl`, and set `credentialsFile` (or `GOOGLE_APPLICATION_CREDENTIALS`) to a service-account JSON key. Set `autoCreate` to false if that identity cannot create topics.

If the built library is `libPubSubPlugin.so` or `libPubSubPlugin.dylib`, set `"name": "libPubSubPlugin"`.

## Config files

| File | Use |
|------|-----|
| `pluginConfig.gateway.json` | Gateway config. Save as `pubsubplugin.json` next to `gg.exe`. |
| `pluginConfig.json` | Same as the gateway file. |
| `pluginConfig.client.json` | Plugin object only (no `configurations` wrapper). |
| `graftConfig.pubsub.example.json` | Client `GraftConfig` JSON. Save as `clientconfig.json`. |
| `pluginConfig.oneway.json` | Fire-and-forget (`oneWay: true`) for both sides. No reply topic. |
