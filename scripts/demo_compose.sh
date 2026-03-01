#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
COMPOSE_FILE="${ROOT_DIR}/deploy/compose/docker-compose.yml"

compose() {
  docker compose -f "${COMPOSE_FILE}" "$@"
}

ctl_node0() {
  compose exec -T node0 deltafs_ctl --endpoint node0:50051 --node-id demo --timeout-ms 8000 "$@"
}

ctl_node1_local() {
  compose exec -T node1 deltafs_ctl --endpoint 127.0.0.1:50051 --node-id demo --timeout-ms 8000 "$@"
}

echo "[demo_compose] starting 3-node cluster"
compose build node0
compose up -d

for i in {1..30}; do
  if ctl_node0 status >/dev/null 2>&1; then
    break
  fi
  sleep 2
done

ctl_node0 status

echo "[demo_compose] writing initial keys"
for i in {1..3}; do
  ctl_node0 put "k${i}" "v${i}"
done

echo "[demo_compose] creating snapshot S1"
SNAP_OUT="$(ctl_node0 snapshot create S1)"
echo "${SNAP_OUT}"
SNAP_ID="$(awk '{print $1}' <<<"${SNAP_OUT}" | cut -d= -f2)"
if [[ -z "${SNAP_ID}" ]]; then
  echo "failed to parse snapshot id" >&2
  exit 1
fi

echo "[demo_compose] mutating keys after snapshot"
ctl_node0 put k1 v1-new
ctl_node0 put k4 v4

echo "[demo_compose] validating snapshot read"
SNAP_READ="$(ctl_node0 snapshot get "${SNAP_ID}" k1)"
echo "${SNAP_READ}"
if ! grep -q 'value="v1"' <<<"${SNAP_READ}"; then
  echo "snapshot validation failed: expected v1" >&2
  exit 1
fi

echo "[demo_compose] stopping follower node1"
compose stop node1

for i in {5..7}; do
  ctl_node0 put "k${i}" "v${i}"
done

echo "[demo_compose] restarting follower node1"
compose start node1

for i in {1..30}; do
  if ctl_node1_local status >/dev/null 2>&1; then
    break
  fi
  sleep 2
done

echo "[demo_compose] trigger catchup and verify"
ctl_node0 put k8 v8
NODE1_READ="$(ctl_node1_local get k8)"
echo "${NODE1_READ}"
if ! grep -q 'value="v8"' <<<"${NODE1_READ}"; then
  echo "node1 catch-up validation failed" >&2
  exit 1
fi

echo "[demo_compose] success"
