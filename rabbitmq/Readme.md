# RabbitMQ Plugin

This plugin carries Graftcode Gateway calls over RabbitMQ (AMQP 0-9-1). It implements `Hypertube::Native::Interfaces::ITransport` and `GraftcodeGateway::IServer`, and exports the transport C ABI `CreateTransport` / `DestroyTransport` / `TransportSendCommand` / `TransportFreeBuffer` (`GraftcodePluginsInterfaces/TransportPluginAbi.h`) and `CreateServer` / `DestroyServer`.

It is written in C++. CMake FetchContent builds [AMQP-CPP](https://github.com/CopernicaMarketingSoftware/AMQP-CPP) `v4.3.27` and nlohmann/json `v3.12.0` into the plugin. There is no vcpkg manifest.

The release archive contains only the plugin shared library (`RabbitmqPlugin.dll`, `libRabbitmqPlugin.so`, or `libRabbitmqPlugin.dylib`). AMQP-CPP is linked into that library. On Windows the MSVC CRT is static (`/MT`), so the VC++ Redistributable is not required. Clients do not install OpenSSL, curl, zlib, or another runtime for this plugin.

Request/reply uses AMQP properties:

- The client sets `correlation-id` and `reply-to` (the configured reply queue).
- The gateway publishes the response to `reply-to` and echoes `correlation-id`.
- A client that receives a different correlation id rejects the message and requeues it, so several clients can share one reply queue.

Queues must already exist. There is no one-way mode. `user` is required. Set `port` to `5672` for the Docker image below; an omitted port is `0` and does not connect. An empty `vhost` means `/`. `rpcTimeoutMs` defaults to `30000` on the client.

## Build

```bash
git clone https://github.com/grft-dev/graftcode-extensions.git
cd graftcode-extensions/rabbitmq

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

Output:

- Windows: `rabbitmq/build/RabbitmqPlugin/RabbitmqPlugin.dll` — config `"name": "RabbitmqPlugin"`. AMQP-CPP and the VC++ runtime are linked in (`/MT`); the VC++ Redistributable is not required.
- Linux/macOS: `rabbitmq/build/RabbitmqPlugin/libRabbitmqPlugin.so` or `.dylib` — config `"name": "libRabbitmqPlugin"`

Download `gg` from https://github.com/grft-dev/graftcode-gateway/releases/.

## Local broker

`rabbitmq/Dockerfile` is `rabbitmq:4-management` (AMQP `5672`, management UI `15672`).

```bash
docker build -t graftcode-rabbitmq .
docker run -d --name graftcode-rabbitmq -p 5672:5672 -p 15672:15672 graftcode-rabbitmq
```

Create `myqueue`. Each client declares its own exclusive reply queue and names it in `reply-to`, so no reply queue needs to be created.

Management UI at `http://localhost:15672` (`guest` / `guest`): **Queues and Streams** → **Add a new queue**.

Or:

```bash
docker exec -it graftcode-rabbitmq rabbitmqadmin declare queue name=myqueue durable=true
```

## Gateway

`pluginConfig.json`:

```json
{
  "name": "RabbitmqPlugin",
  "host": "localhost",
  "port": 5672,
  "queue": "myqueue",
  "user": "guest",
  "password": "guest",
  "vhost": "/",
  "rpcTimeoutMs": 30000
}
```

```powershell
./gg .\PhysicsCalculator.dll --config .\pluginConfig.json
```

Open `http://localhost:81/GV` to install the generated package, or call the Gateway from .NET with the Hypertube SDK (see [Client configuration](#client-configuration)).

## Client configuration

A .NET client can call the gateway through the Hypertube SDK. The client config has no `runtime` or `host` keys; the plugin carries the transport. Port `5673` and user `graftcode` match [samples/rabbitmq](../samples/rabbitmq). The standalone broker above uses `5672` and `guest`.

```json
{
  "configurations": {
    "graft.nuget.netapp": {
      "plugin": {
        "name": "RabbitmqPlugin",
        "host": "localhost",
        "port": 5673,
        "queue": "myqueue",
        "user": "graftcode",
        "password": "graftcode",
        "vhost": "/",
        "rpcTimeoutMs": 30000
      }
    }
  }
}
```

```csharp
using Hypertube.Netcore.Sdk;
using Hypertube.Netcore.Sdk.Configuration;

RuntimeBridge.AddConfig(ConfigPriority.User, configJsonOrPathToFile);
var rtmCtx = RuntimeBridge.InitializeRC("graft.nuget.netapp");
var result = rtmCtx.GetType("netapp.Calculator").CreateInstance()
    .InvokeInstanceMethod("Add", 1, 2).Execute().GetValue();
```

`AddConfig` accepts a JSON string, a path to a JSON file, or the name of an environment variable holding either. The key under `configurations` must match the name passed to `InitializeRC`. Load one plugin config per process.

## Configuration reference

| Field | Required | Description |
|-------|----------|-------------|
| `name` | yes | Plugin library name without extension. |
| `host` | yes | Broker hostname. |
| `port` | yes | AMQP port. `5672` for the Docker image. |
| `queue` | yes | Request queue. |
| `user` | yes | AMQP username. |
| `password` | no | AMQP password. |
| `vhost` | no | Virtual host. Empty means `/`. |
| `rpcTimeoutMs` | no | Client wait for a matching reply. Default `30000`. |
