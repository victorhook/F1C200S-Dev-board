#!/usr/bin/env python3
"""
Decode UYVY 640x480 frames captured by csi-capture to viewable PGM files.

Our csi-capture tool dumps raw UYVY bytes (2 bytes per pixel) with no header.
OV2640 via CSI RAW passthrough emits UYVY (not YUYV) — Y lives at odd byte
offsets, U/V at even offsets.

This script:
  - Extracts the Y (luma) plane and writes a grayscale PGM
  - Optionally does percentile-based contrast stretching (default on) so
    low-contrast captures actually look like something
  - Prints per-frame Y min/max/avg and the 2%/98% percentiles so you can
    tell at a glance whether the scene had contrast

Usage:
    ./decode-uyvy.py capture.yuyv                  # → capture.pgm
    ./decode-uyvy.py /tmp/*.yuyv                   # batch
    ./decode-uyvy.py -o out/ caps/*.yuyv           # pick output dir
    ./decode-uyvy.py --no-stretch cap.yuyv         # raw Y plane, no stretching
    ./decode-uyvy.py --yuyv cap.yuyv               # treat byte order as YUYV
    ./decode-uyvy.py --size 320x240 cap.yuyv       # different resolution

View with any image viewer: feh / eog / gimp / display.
"""

import argparse
import os
import sys


def y_plane(raw: bytes, byte_order: str) -> bytes:
    """Pick every other byte to extract the Y plane."""
    if byte_order == "uyvy":
        # U-Y-V-Y: Y is at odd offsets (1, 3, 5, ...)
        return bytes(raw[i] for i in range(1, len(raw), 2))
    else:  # "yuyv"
        # Y-U-Y-V: Y is at even offsets (0, 2, 4, ...)
        return bytes(raw[i] for i in range(0, len(raw), 2))


def stretch(y: bytes, low_pct: float = 0.02, high_pct: float = 0.98):
    """Map the [p_low, p_high] quantile range of Y to [0, 255]."""
    hist = [0] * 256
    for b in y:
        hist[b] += 1
    total = len(y)
    cum = 0
    lo = 0
    hi = 255
    found_lo = False
    for i, c in enumerate(hist):
        cum += c
        if not found_lo and cum >= total * low_pct:
            lo = i
            found_lo = True
        if cum >= total * high_pct:
            hi = i
            break
    if hi <= lo:
        # flat image — just pass through
        return y, lo, hi
    scale = 255.0 / (hi - lo)
    stretched = bytes(
        max(0, min(255, int((b - lo) * scale))) for b in y
    )
    return stretched, lo, hi


def decode_one(path: str, width: int, height: int, byte_order: str,
               do_stretch: bool, out_dir: str | None) -> None:
    expected = width * height * 2
    with open(path, "rb") as f:
        raw = f.read()
    if len(raw) < expected:
        print(f"{path}: truncated — {len(raw)} bytes, expected ≥ {expected}",
              file=sys.stderr)
        return
    if len(raw) != expected:
        print(f"{path}: size {len(raw)} != expected {expected} "
              f"({width}x{height} {byte_order.upper()}), using first {expected}",
              file=sys.stderr)
        raw = raw[:expected]

    y = y_plane(raw, byte_order)
    assert len(y) == width * height, f"{len(y)} != {width*height}"

    ymin, ymax = min(y), max(y)
    yavg = sum(y) / len(y)

    if do_stretch:
        out_y, p_lo, p_hi = stretch(y)
        stretch_info = f"  stretch [{p_lo:3d}..{p_hi:3d}]→[0..255]"
    else:
        out_y = y
        stretch_info = ""

    base = os.path.basename(path)
    name, _ = os.path.splitext(base)
    out_name = f"{name}.pgm"
    out_path = os.path.join(out_dir, out_name) if out_dir else \
               os.path.join(os.path.dirname(path) or ".", out_name)

    with open(out_path, "wb") as f:
        f.write(f"P5\n{width} {height}\n255\n".encode())
        f.write(out_y)

    print(f"{base}: Y {ymin:3d}..{ymax:3d} avg {yavg:5.1f}{stretch_info}  "
          f"→  {out_path}")


def parse_size(s: str) -> tuple[int, int]:
    w, _, h = s.lower().partition("x")
    if not w or not h:
        raise argparse.ArgumentTypeError("size must be WIDTHxHEIGHT")
    return int(w), int(h)


def main() -> int:
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    ap.add_argument("files", nargs="+",
                    help="one or more YUYV/UYVY raw files")
    ap.add_argument("-o", "--out-dir", default=None,
                    help="output directory (default: next to input)")
    ap.add_argument("--size", type=parse_size, default=(640, 480),
                    help="frame size (default 640x480)")
    order = ap.add_mutually_exclusive_group()
    order.add_argument("--uyvy", dest="order", action="store_const",
                       const="uyvy", help="byte order UYVY (default)")
    order.add_argument("--yuyv", dest="order", action="store_const",
                       const="yuyv", help="byte order YUYV")
    ap.set_defaults(order="uyvy")
    ap.add_argument("--no-stretch", dest="stretch", action="store_false",
                    help="disable contrast stretching")
    args = ap.parse_args()

    w, h = args.size
    if args.out_dir:
        os.makedirs(args.out_dir, exist_ok=True)

    for path in args.files:
        decode_one(path, w, h, args.order, args.stretch, args.out_dir)

    return 0


if __name__ == "__main__":
    sys.exit(main())
