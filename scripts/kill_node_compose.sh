#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
COMPOSE_FILE="${ROOT_DIR}/deploy/compose/docker-compose.yml"
NODE_NAME="${1:-node1}"

docker compose -f "${COMPOSE_FILE}" stop "${NODE_NAME}"
echo "stopped ${NODE_NAME}"
