#!/usr/bin/env bash
set -euo pipefail

POD_NAME="${1:-deltafs-1}"

kubectl delete pod "${POD_NAME}" --wait=false

echo "deleted pod ${POD_NAME}"
