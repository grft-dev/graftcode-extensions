#!/usr/bin/env bash
# Create the default Graftcode Kafka request/reply topics.
set -euo pipefail

BROKERS="${BROKERS:-localhost:9092}"
REQUEST_TOPIC="${REQUEST_TOPIC:-graft.requests}"
REPLY_TOPIC="${REPLY_TOPIC:-graft.replies}"
CONTAINER="${KAFKA_CONTAINER:-}"

create_with_kafka_topics() {
  local bin="$1"
  "$bin" --bootstrap-server "$BROKERS" --create --if-not-exists --topic "$REQUEST_TOPIC" --partitions 1 --replication-factor 1
  "$bin" --bootstrap-server "$BROKERS" --create --if-not-exists --topic "$REPLY_TOPIC" --partitions 1 --replication-factor 1
}

if [[ -n "$CONTAINER" ]]; then
  echo "Creating topics via docker exec on container '$CONTAINER' (bootstrap $BROKERS)..."
  # apache/kafka image path
  if docker exec "$CONTAINER" test -x /opt/kafka/bin/kafka-topics.sh; then
    docker exec "$CONTAINER" /opt/kafka/bin/kafka-topics.sh --bootstrap-server localhost:9092 \
      --create --if-not-exists --topic "$REQUEST_TOPIC" --partitions 1 --replication-factor 1
    docker exec "$CONTAINER" /opt/kafka/bin/kafka-topics.sh --bootstrap-server localhost:9092 \
      --create --if-not-exists --topic "$REPLY_TOPIC" --partitions 1 --replication-factor 1
  # bitnami / other layouts
  elif docker exec "$CONTAINER" sh -c 'command -v kafka-topics.sh' >/dev/null 2>&1; then
    docker exec "$CONTAINER" kafka-topics.sh --bootstrap-server localhost:9092 \
      --create --if-not-exists --topic "$REQUEST_TOPIC" --partitions 1 --replication-factor 1
    docker exec "$CONTAINER" kafka-topics.sh --bootstrap-server localhost:9092 \
      --create --if-not-exists --topic "$REPLY_TOPIC" --partitions 1 --replication-factor 1
  else
    echo "Could not find kafka-topics.sh inside $CONTAINER" >&2
    exit 1
  fi
elif command -v kafka-topics.sh >/dev/null 2>&1; then
  create_with_kafka_topics kafka-topics.sh
elif command -v rpk >/dev/null 2>&1; then
  echo "Creating topics via rpk (brokers $BROKERS)..."
  rpk topic create "$REQUEST_TOPIC" -X brokers="$BROKERS" || true
  rpk topic create "$REPLY_TOPIC" -X brokers="$BROKERS" || true
else
  # Auto-detect a running compose kafka container
  CONTAINER="$(docker ps --format '{{.Names}}' | grep -E 'kafka|redpanda' | head -n1 || true)"
  if [[ -z "$CONTAINER" ]]; then
    echo "No kafka-topics.sh/rpk on PATH and no kafka/redpanda container running." >&2
    echo "Start the broker first: docker compose up -d" >&2
    echo "Or set KAFKA_CONTAINER=<name>." >&2
    exit 1
  fi
  KAFKA_CONTAINER="$CONTAINER" "$0"
  exit $?
fi

echo "Topics ready: $REQUEST_TOPIC, $REPLY_TOPIC"
