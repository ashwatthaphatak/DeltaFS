#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/../../.." && pwd)"

if ! command -v kubectl >/dev/null 2>&1; then
  echo "SKIP: kubectl not installed"
  exit 0
fi

if ! kubectl cluster-info >/dev/null 2>&1; then
  echo "SKIP: no kubernetes cluster available"
  exit 0
fi

"${ROOT_DIR}/scripts/demo_k8s.sh"
