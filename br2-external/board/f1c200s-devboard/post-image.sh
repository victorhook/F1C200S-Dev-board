#!/bin/sh
# Post-image script - creates the final SD card image
set -e

BOARD_DIR="$(dirname "$0")"

# Generate boot.scr from boot.cmd
"${HOST_DIR}/bin/mkimage" -C none -A arm -T script \
    -d "${BOARD_DIR}/boot.cmd" \
    "${BINARIES_DIR}/boot.scr"

# Generate SD card image
support/scripts/genimage.sh -c "${BOARD_DIR}/genimage.cfg"
