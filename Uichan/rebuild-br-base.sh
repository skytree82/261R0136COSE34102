#!/usr/bin/env bash
set -Ee
set -o pipefail

CHIPYARD_DIR="$HOME/chipyard3"
FIREMARSHAL_DIR="$CHIPYARD_DIR/software/firemarshal"
WORKLOAD="br-base.json"

cd "$CHIPYARD_DIR"

if [[ ! -f env.sh ]]; then
  echo "missing Chipyard env.sh in $CHIPYARD_DIR" >&2
  exit 1
fi

source env.sh

cd "$FIREMARSHAL_DIR"

if [[ ! -x marshal ]]; then
  echo "missing executable marshal in $FIREMARSHAL_DIR" >&2
  exit 1
fi

./marshal clean "$WORKLOAD"
./marshal -v build "$WORKLOAD"
./marshal -v install "$WORKLOAD"
