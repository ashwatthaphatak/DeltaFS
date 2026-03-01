#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
OVERLAY="${ROOT_DIR}/deploy/k8s/overlays/minikube"

if ! command -v kubectl >/dev/null 2>&1; then
  echo "kubectl is required" >&2
  exit 1
fi

if command -v minikube >/dev/null 2>&1 && minikube status >/dev/null 2>&1; then
  eval "$(minikube -p minikube docker-env)"
fi

echo "[demo_k8s] building image deltafs:dev"
docker build -t deltafs:dev -f "${ROOT_DIR}/deploy/Dockerfile" "${ROOT_DIR}"

echo "[demo_k8s] applying manifests"
kubectl apply -k "${OVERLAY}"

kubectl rollout status statefulset/deltafs --timeout=180s
kubectl wait --for=condition=Ready pod/deltafs-0 pod/deltafs-1 pod/deltafs-2 --timeout=180s

kctl() {
  local pod="$1"
  shift
  kubectl exec "${pod}" -- deltafs_ctl --endpoint deltafs-0.deltafs-headless:50051 --node-id demo --timeout-ms 8000 "$@"
}

kctl_local() {
  local pod="$1"
  shift
  kubectl exec "${pod}" -- deltafs_ctl --endpoint 127.0.0.1:50051 --node-id demo --timeout-ms 8000 "$@"
}

kctl deltafs-0 status

for i in {1..3}; do
  kctl deltafs-0 put "k${i}" "v${i}"
done

SNAP_OUT="$(kctl deltafs-0 snapshot create S1)"
echo "${SNAP_OUT}"
SNAP_ID="$(awk '{print $1}' <<<"${SNAP_OUT}" | cut -d= -f2)"
if [[ -z "${SNAP_ID}" ]]; then
  echo "failed to parse snapshot id" >&2
  exit 1
fi

kctl deltafs-0 put k1 v1-new
kctl deltafs-0 put k4 v4

SNAP_READ="$(kctl deltafs-0 snapshot get "${SNAP_ID}" k1)"
echo "${SNAP_READ}"
if ! grep -q 'value="v1"' <<<"${SNAP_READ}"; then
  echo "snapshot validation failed" >&2
  exit 1
fi

echo "[demo_k8s] deleting follower deltafs-1"
kubectl delete pod deltafs-1 --wait=false

for i in {5..7}; do
  kctl deltafs-0 put "k${i}" "v${i}"
done

kubectl wait --for=condition=Ready pod/deltafs-1 --timeout=180s

kctl deltafs-0 put k8 v8
NODE1_READ="$(kctl_local deltafs-1 get k8)"
echo "${NODE1_READ}"
if ! grep -q 'value="v8"' <<<"${NODE1_READ}"; then
  echo "deltafs-1 catch-up validation failed" >&2
  exit 1
fi

echo "[demo_k8s] success"
