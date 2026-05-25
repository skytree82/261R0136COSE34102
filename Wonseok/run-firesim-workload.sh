#!/usr/bin/env bash
set -Ee
set -o pipefail

FIRESIM_DIR="$HOME/chipyard3/sims/firesim"

cd "$FIRESIM_DIR"

if [[ ! -f sourceme-manager.sh ]]; then
  echo "missing FireSim manager setup script in $FIRESIM_DIR" >&2
  exit 1
fi

source sourceme-manager.sh

firesim kill
firesim infrasetup
firesim runworkload
