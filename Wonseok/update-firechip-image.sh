#!/usr/bin/env bash
set -Eeuo pipefail

IMG="$HOME/chipyard3/software/firemarshal/images/firechip/br-base/br-base.img"
SRC="$HOME/chipyard3/Kyu/Evaluation_tool"
MNT="/mnt/hostdisk"
DST="$HOME/chipyard3/sims/firesim/sim_slot_0/rsyncdir/br-base0-br-base.img"

TARGET_BYTES=$((4 * 1024 * 1024 * 1024))
LOOP=""

cleanup() {
  set +e
  if mountpoint -q "$MNT"; then
    sudo umount "$MNT"
  fi
  if [[ -n "${LOOP:-}" ]] && sudo losetup "$LOOP" >/dev/null 2>&1; then
    sudo losetup -d "$LOOP"
  fi
}
trap cleanup EXIT

[[ -f "$IMG" ]] || { echo "missing image: $IMG"; exit 1; }
[[ -d "$SRC" ]] || { echo "missing source dir: $SRC"; exit 1; }

current_bytes=$(stat -c '%s' "$IMG")

if (( current_bytes < TARGET_BYTES )); then
  sudo truncate -s 4G "$IMG"
elif (( current_bytes > TARGET_BYTES )); then
  echo "image is already larger than 4G; refusing to shrink it"
  exit 1
fi

sudo mkdir -p "$MNT"

LOOP=$(sudo losetup -f --show "$IMG")

sudo e2fsck -f -y "$LOOP"
sudo resize2fs "$LOOP"

sudo mount "$LOOP" "$MNT"

sudo mkdir -p "$MNT/media"
sudo find "$MNT/media" -mindepth 1 -maxdepth 1 -exec rm -rf -- {} +
sudo cp -a "$SRC"/. "$MNT/media"/

sudo sync
sudo umount "$MNT"

sudo losetup -d "$LOOP"
LOOP=""

trap - EXIT

mkdir -p "$(dirname "$DST")"

rsync -avh --progress --inplace \
  "$IMG" \
  "$DST"
