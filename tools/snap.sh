#!/usr/bin/env bash
#
# snap.sh  —  one-shot camera snap pipeline: capture, fetch, decode, view.
#
# What it does:
#   1. SSH to the board and capture N frames via yavta (default N=5; the
#      first 2-3 are sensor warmup so we throw them away and use the last).
#   2. scp the chosen frame back to the host.
#   3. Decode YUYV → grayscale PGM with percentile contrast stretching
#      (tools/decode-uyvy.py).
#   4. Open the PGM in whatever image viewer xdg-open resolves to;
#      falls back to feh, then to printing the path.
#
# Usage:
#   ./tools/snap.sh                  # default: 3 frames (last is taken)
#   COUNT=10 ./tools/snap.sh         # capture more if AGC seems slow
#   BOARD=root@10.0.0.5 ./tools/snap.sh
#   ./tools/snap.sh /tmp/myshot.yuyv # custom output path
#   GRAY=1 ./tools/snap.sh           # grayscale PGM output instead of color PPM
#
# Speed tip: SSH on the F1C200S is slow because the ARM9 takes ~1 s for
# the crypto handshake.  This script already collapses capture+fetch into
# a single ssh invocation (the dominant cost is *one* handshake, not two),
# but you can amortise even that by enabling SSH connection multiplexing
# in your ~/.ssh/config:
#
#   Host 192.168.100.1
#       ControlMaster auto
#       ControlPath ~/.ssh/cm-%r@%h:%p
#       ControlPersist 10m
#
# After the first connection of the day, the master socket is reused for
# 10 minutes — every subsequent snap.sh runs in ~half a second total.
#
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
BOARD=${BOARD:-board}                 # alias from tools/ssh-config
COUNT=${COUNT:-20}                    # OV2640 AGC needs ~15 frames to converge
OUT=${1:-/tmp/snap.yuyv}
YAVTA=${YAVTA:-/root/yavta}           # not in $PATH on the board
SSH=(ssh -F "$HERE/ssh-config")       # speed-tuned (chacha20, curve25519, ControlMaster)
IMG="${OUT%.yuyv}.ppm"   # color output by default; --gray flag below switches
LAST_IDX=$((COUNT - 1))
# OV2640 + sun4i-csi RAW passthrough emits Y-V-Y-U on the wire (YUYV
# byte order with U/V swapped vs the canonical YUYV pixel format
# layout).  Override with ORDER=yuyv if you ever change the OV2640
# subdev's chroma sequencing register.
ORDER=${ORDER:-yvyu}
DECODE_FLAGS=("--$ORDER" --color)
if [[ "${GRAY:-0}" == "1" ]]; then
    IMG="${OUT%.yuyv}.pgm"
    DECODE_FLAGS=("--$ORDER")
fi

echo "[1/3] capture+fetch ($COUNT frames on $BOARD, taking frame $LAST_IDX)"
# Single ssh: capture, then cat the chosen frame to stdout.  yavta's noisy
# stderr/stdout go to /dev/null so only the binary frame lands in $OUT.
# This avoids a second crypto handshake (a separate scp would double the
# board-side auth cost, which is the slow part on this SoC).
LAST_FILE=$(printf "/tmp/snap-%06d.yuyv" "$LAST_IDX")
"${SSH[@]}" "$BOARD" "$YAVTA --format=YUYV --size=640x480 --capture=$COUNT \
              --file=/tmp/snap-#.yuyv /dev/video0 >/dev/null 2>&1 && \
              cat $LAST_FILE" > "$OUT"

echo "[3/3] decoding → $IMG"
"$HERE/decode-uyvy.py" "${DECODE_FLAGS[@]}" "$OUT" -o "$(dirname "$OUT")" \
    | sed 's/^/      /'

echo "      opening..."
if command -v xdg-open >/dev/null; then
    xdg-open "$IMG" >/dev/null 2>&1 &
elif command -v feh >/dev/null; then
    feh "$IMG" &
else
    echo "no image viewer found; open manually: $IMG"
fi
