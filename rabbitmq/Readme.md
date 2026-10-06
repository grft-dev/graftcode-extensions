# RabbitMQ Plugin

This plugin carries Graftcode Gateway calls over RabbitMQ (AMQP 0-9-1). It implements `Hypertube::Native::Interfaces::ITransport` and `GraftcodeGateway::IServer`, and exports `CreateTransportChannel` / `DestroyTransportChannel` and `CreateServer` / `DestroyServer`.

It is written in C++. CMake FetchContent builds [AMQP-CPP](https://github.com/CopernicaMarketingSoftware/AMQP-CPP) `v4.3.27` and nlohmann/json `v3.12.0` into the plugin. There is no vcpkg manifest.

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

- Windows: `rabbitmq/build/RabbitmqPlugin/RabbitmqPlugin.dll` — config `"name": "RabbitmqPlugin"`
- Linux/macOS: `rabbitmq/build/RabbitmqPlugin/libRabbitmqPlugin.so` or `.dylib` — config `"name": "libRabbitmqPlugin"`

Download `gg` from https://github.com/grft-dev/graftcode-gateway/releases/.

## Local broker

`rabbitmq/Dockerfile` is `rabbitmq:4-management` (AMQP `5672`, management UI `15672`).

```bash
docker build -t graftcode-rabbitmq .
docker run -d --name graftcode-rabbitmq -p 5672:5672 -p 15672:15672 graftcode-rabbitmq
```

Create `myqueue` and `myqueue.reply`.

Management UI at `http://localhost:15672` (`guest` / `guest`): **Queues and Streams** → **Add a new queue**.

Or:

```bash
docker exec -it graftcode-rabbitmq rabbitmqadmin declare queue name=myqueue durable=true
docker exec -it graftcode-rabbitmq rabbitmqadmin declare queue name=myqueue.reply durable=true
```

## Gateway

`pluginConfig.json`:

```json
{
  "name": "RabbitmqPlugin",
  "host": "localhost",
  "port": 5672,
  "queue": "myqueue",
  "replyQueue": "myqueue.reply",
  "user": "guest",
  "password": "guest",
  "vhost": "/",
  "rpcTimeoutMs": 30000
}
```

```powershell
./gg .\PhysicsCalculator.dll --config .\pluginConfig.json
```

Open `http://localhost:81/GV` and install the generated package.

## Client

`host` inside `configurations` is the Gateway address. Broker host and port stay in `plugin`.

```csharp
string configSource =
"""
{
  "configurations": {
    "graft.nuget.PhysicsCalculator": {
      "runtime": "netcore",
      "host": "localhost:80",
      "stateless": true,
      "plugin": {
        "name": "RabbitmqPlugin",
        "host": "localhost",
        "port": 5672,
        "queue": "myqueue",
        "replyQueue": "myqueue.reply",
        "user": "guest",
        "password": "guest",
        "vhost": "/",
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
| `host` | yes | Broker hostname. |
| `port` | yes | AMQP port. `5672` for the Docker image. |
| `queue` | yes | Request queue. |
| `replyQueue` | yes (client) | Queue named by `reply-to`. The gateway reads it from the message. |
| `user` | yes | AMQP username. |
| `password` | no | AMQP password. |
| `vhost` | no | Virtual host. Empty means `/`. |
| `rpcTimeoutMs` | no | Client wait for a matching reply. Default `30000`. |
