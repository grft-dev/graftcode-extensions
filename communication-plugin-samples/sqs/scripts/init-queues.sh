#!/bin/sh
set -e

awslocal sqs create-queue --queue-name graft-requests
awslocal sqs create-queue --queue-name graft-replies