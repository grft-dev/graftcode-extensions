# Kafka sample configs

Local broker + UI and Graftcode plugin connection files.

## Start Kafka

```bash
cd samples/kafka
docker compose up -d
```

- Broker: `localhost:9092`
- UI: http://localhost:8080

Create topics (if needed):

```bash
docker exec graftcode-kafka /opt/kafka/bin/kafka-topics.sh --bootstrap-server localhost:9092 --create --if-not-exists --topic graft.requests --partitions 1 --replication-factor 1
docker exec graftcode-kafka /opt/kafka/bin/kafka-topics.sh --bootstrap-server localhost:9092 --create --if-not-exists --topic graft.replies --partitions 1 --replication-factor 1
```

## Config files

| File | Use |
|------|-----|
| `pluginConfig.gateway.json` | `gg YourModule.dll --config pluginConfig.gateway.json` |
| `pluginConfig.client.json` | Client-side plugin block |
| `graftConfig.kafka.example.json` | Full GraftConfig example for .NET |
| `pluginConfig.json` | Same as gateway (default name) |
