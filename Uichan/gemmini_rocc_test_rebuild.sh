#!/usr/bin/env bash

set -euo pipefail

CHIPYARD_DIR="/home/esca/chipyard3"
TEST_DIR="$CHIPYARD_DIR/generators/gemmini/software/gemmini-rocc-tests"
BUILD_DIR="$TEST_DIR/build/imagenet"
DEST_DIR="$CHIPYARD_DIR/Kyu/Evaluation_tool"

ARTIFACTS=(
  "resnet50_irq_rvv_no_op_wait-linux"
  "resnet50_irq_rvv-linux"
  "mobilenet_irq_rvv_all_vpu_wait-linux"
  "resnet50_irq_rvv_all_vpu_wait-linux"
  "resnet50_hwfence-linux"
)

if [[ ! -d "$CHIPYARD_DIR" ]]; then
  echo "Missing chipyard directory: $CHIPYARD_DIR" >&2
  exit 1
fi

if [[ ! -f "$CHIPYARD_DIR/env.sh" ]]; then
  echo "Missing env.sh: $CHIPYARD_DIR/env.sh" >&2
  exit 1
fi

if [[ ! -d "$TEST_DIR" ]]; then
  echo "Missing test directory: $TEST_DIR" >&2
  exit 1
fi

if [[ ! -d "$DEST_DIR" ]]; then
  echo "Missing destination directory: $DEST_DIR" >&2
  exit 1
fi

echo "[1/3] Sourcing $CHIPYARD_DIR/env.sh"
cd "$CHIPYARD_DIR"

# chipyard env activation may reference variables before they are initialized.
# Keep strict mode overall, but relax nounset while sourcing env.sh.
set +u
source "$CHIPYARD_DIR/env.sh"
set -u

echo "[2/3] Building gemmini-rocc-tests"
cd "$TEST_DIR"
bash ./build.sh

echo "[3/3] Copying build artifacts to $DEST_DIR"
for artifact in "${ARTIFACTS[@]}"; do
  src="$BUILD_DIR/$artifact"

  if [[ ! -f "$src" ]]; then
    echo "Missing build artifact: $src" >&2
    exit 1
  fi

  cp -f "$src" "$DEST_DIR/"
  echo "Copied: $artifact"
done

echo "Build and copy completed successfully."
