#!/usr/bin/env bash
set -Eeuo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo "[1/3] Rebuilding br-base with FireMarshal..."
"$SCRIPT_DIR/rebuild-br-base.sh"

echo "[2/3] Updating FireChip image..."
"$SCRIPT_DIR/update-firechip-image.sh"

echo "[3/3] Running FireSim workload..."
"$SCRIPT_DIR/run-firesim-workload.sh"
