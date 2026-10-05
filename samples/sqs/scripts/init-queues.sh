#!/bin/sh
# Runs inside LocalStack once the edge port is ready.
set -eu

awslocal sqs create-queue --queue-name graft-requests
awslocal sqs create-queue --queue-name graft-replies
echo "SQS queues ready: graft-requests, graft-replies"
