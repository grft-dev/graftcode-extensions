#!/usr/bin/env bash
# Create the sample topic and subscription against an emulator already listening.
set -euo pipefail

HOST="${PUBSUB_EMULATOR_HOST:-localhost:8085}"
HOST="${HOST#http://}"
HOST="${HOST#https://}"
HOST="${HOST%/}"
PROJECT="${PUBSUB_PROJECT_ID:-graftcode-local}"
BASE="http://${HOST}/v1/projects/${PROJECT}"

put() {
  local url="$1"
  local body="$2"
  local status
  status="$(curl -sS -o /tmp/graftcode-pubsub-create.txt -w "%{http_code}" \
    -X PUT "$url" \
    -H "Content-Type: application/json" \
    -d "$body")"
  if [[ "$status" != "200" && "$status" != "409" ]]; then
    echo "PUT $url failed: HTTP $status" >&2
    cat /tmp/graftcode-pubsub-create.txt >&2 || true
    exit 1
  fi
}

put "${BASE}/topics/graft-requests" \
  "{\"name\":\"projects/${PROJECT}/topics/graft-requests\"}"
put "${BASE}/topics/graft-replies" \
  "{\"name\":\"projects/${PROJECT}/topics/graft-replies\"}"
put "${BASE}/subscriptions/graft-requests-sub" \
  "{\"name\":\"projects/${PROJECT}/subscriptions/graft-requests-sub\",\"topic\":\"projects/${PROJECT}/topics/graft-requests\",\"ackDeadlineSeconds\":60}"
put "${BASE}/subscriptions/graft-replies-sub" \
  "{\"name\":\"projects/${PROJECT}/subscriptions/graft-replies-sub\",\"topic\":\"projects/${PROJECT}/topics/graft-replies\",\"ackDeadlineSeconds\":60}"

echo "Pub/Sub resources ready on ${HOST} in project ${PROJECT}"
echo "  topic graft-requests  subscription graft-requests-sub"
echo "  topic graft-replies   subscription graft-replies-sub"
