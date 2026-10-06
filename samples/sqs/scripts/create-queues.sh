#!/usr/bin/env bash
# Create the sample queues against a LocalStack already listening on localhost.
set -euo pipefail

ENDPOINT="${SQS_ENDPOINT_OVERRIDE:-http://localhost:4566}"
REGION="${AWS_REGION:-us-east-1}"
export AWS_ACCESS_KEY_ID="${AWS_ACCESS_KEY_ID:-test}"
export AWS_SECRET_ACCESS_KEY="${AWS_SECRET_ACCESS_KEY:-test}"
export AWS_DEFAULT_REGION="$REGION"

if ! command -v aws >/dev/null 2>&1; then
  echo "aws CLI is required (https://docs.aws.amazon.com/cli/latest/userguide/getting-started-install.html)" >&2
  exit 1
fi

aws --endpoint-url "$ENDPOINT" sqs create-queue --queue-name graft-requests --region "$REGION"
aws --endpoint-url "$ENDPOINT" sqs create-queue --queue-name graft-replies --region "$REGION"

echo "Request queue:"
aws --endpoint-url "$ENDPOINT" sqs get-queue-url --queue-name graft-requests --region "$REGION" --output text
echo "Reply queue:"
aws --endpoint-url "$ENDPOINT" sqs get-queue-url --queue-name graft-replies --region "$REGION" --output text
