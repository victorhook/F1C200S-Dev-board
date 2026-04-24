#!/usr/bin/env python3
"""
Analyze a PulseView "every-sample" CSV export: find HREF-high bursts, count
PCLK edges within each, report stats.

PulseView's raw export looks like:
    logic,logic,logic,logic,logic,logic,logic,logic
    1,1,1,1,1,1,0,1
    ...
    (one row per sample, no timestamp, columns are channels 0..N)

So you need to tell this script:
 - sample rate (-r, MHz) — set in PulseView when capturing
 - which column is PCLK (--pclk, 0-indexed)
 - which column is HREF (--href, 0-indexed)
 - which column is VSYNC (--vsync, optional, for frame stats)

Example — if you wired channels in this order: D0=empty, D1=VSYNC, D2=PCLK,
D3=HREF, D4..D7=DATA, and captured at 24 MHz:

    ./analyze-la.py capture.csv -r 24 --vsync 1 --pclk 2 --href 3

To keep runtime reasonable on huge captures, pass --max-samples to stop after
processing N samples (24M samples = 1s at 24 MHz — usually plenty for diag).
"""

import argparse
import statistics
import sys
import time


def analyze(path, sample_rate_mhz, pclk_col, href_col, vsync_col=None,
            max_samples=None):
	ts_per_sample = 1.0 / (sample_rate_mhz * 1_000_000)
	bursts = []          # (href_duration_s, pclk_count)
	vsync_periods = []   # seconds between VSYNC rising edges

	prev_pclk = prev_href = prev_vsync = 0
	href_start_idx = None
	pclk_in_burst = 0
	vsync_last_rising_idx = None

	start_t = time.time()
	sample_idx = 0

	with open(path) as f:
		header = f.readline()    # discard "logic,logic,..."

		for line in f:
			# Manual split is ~3× faster than csv.reader on 175M rows
			parts = line.rstrip("\n").split(",")
			if sample_idx == 0:
				prev_pclk = int(parts[pclk_col])
				prev_href = int(parts[href_col])
				if vsync_col is not None:
					prev_vsync = int(parts[vsync_col])
				sample_idx += 1
				continue

			pclk = int(parts[pclk_col])
			href = int(parts[href_col])

			# HREF edges
			if prev_href == 0 and href == 1:
				href_start_idx = sample_idx
				pclk_in_burst = 0
			elif prev_href == 1 and href == 0 and href_start_idx is not None:
				dur = (sample_idx - href_start_idx) * ts_per_sample
				bursts.append((dur, pclk_in_burst))
				href_start_idx = None

			# PCLK rising edges during HREF HIGH
			if href == 1 and prev_pclk == 0 and pclk == 1:
				pclk_in_burst += 1

			# VSYNC rising edges — measure frame period
			if vsync_col is not None:
				vsync = int(parts[vsync_col])
				if prev_vsync == 0 and vsync == 1:
					if vsync_last_rising_idx is not None:
						vsync_periods.append(
							(sample_idx - vsync_last_rising_idx) * ts_per_sample
						)
					vsync_last_rising_idx = sample_idx
				prev_vsync = vsync

			prev_pclk = pclk
			prev_href = href
			sample_idx += 1

			if max_samples is not None and sample_idx >= max_samples:
				break

			# Progress heartbeat every 10M samples
			if sample_idx % 10_000_000 == 0:
				elapsed = time.time() - start_t
				print(f"  {sample_idx/1e6:.0f}M samples "
					  f"({sample_idx * ts_per_sample:.2f}s of capture), "
					  f"{len(bursts)} HREF bursts so far "
					  f"[{elapsed:.1f}s wall]",
					  file=sys.stderr)

	return bursts, vsync_periods, sample_idx


def interpret(avg_cycles):
	ranges = [
		(1200, 1350, "full VGA line (640 px YUYV).",
			"Sensor side fine. CSI dropping data → driver/register bug."),
		(1550, 1700, "SVGA-ish line (~800 px YUYV).",
			"Sensor in SVGA mode, CSI expects VGA → truncation."),
		(3100, 3300, "UXGA-raw line (~1600 px YUYV).",
			"DSP zoom NOT active, sensor outputting raw UXGA."),
		(290,  330,  "~160 px per line (QQVGA-ish).",
			"Matches 160-byte-per-row capture pattern exactly → DSP downscale bug."),
	]
	for lo, hi, what, implication in ranges:
		if lo <= avg_cycles <= hi:
			return f"{what}\n  {implication}"
	return ("unusual value; compare vs VGA=1280, SVGA=1600, UXGA=3200, QQVGA=320.")


def main():
	ap = argparse.ArgumentParser(description=__doc__,
								  formatter_class=argparse.RawDescriptionHelpFormatter)
	ap.add_argument("csv", help="PulseView CSV export (one sample per row)")
	ap.add_argument("-r", "--sample-rate", type=float, required=True,
					help="LA sample rate in MHz (e.g. 24)")
	ap.add_argument("--pclk",  type=int, required=True,
					help="0-indexed column number of PCLK channel")
	ap.add_argument("--href",  type=int, required=True,
					help="0-indexed column number of HREF channel")
	ap.add_argument("--vsync", type=int, default=None,
					help="0-indexed column number of VSYNC channel (optional)")
	ap.add_argument("--max-samples", type=int, default=24_000_000,
					help="stop after N samples (default 24M = 1s @ 24 MHz)")
	args = ap.parse_args()

	print(f"Processing {args.csv} at {args.sample_rate} MHz sample rate "
		  f"(limit: {args.max_samples/1e6:.0f}M samples)")
	bursts, vsync_periods, total = analyze(
		args.csv, args.sample_rate, args.pclk, args.href,
		args.vsync, args.max_samples)

	print(f"\nTotal samples processed: {total/1e6:.2f}M "
		  f"= {total / (args.sample_rate*1e6):.3f}s of capture")

	if not bursts:
		print("\nNo HREF bursts found. Double-check --pclk/--href columns.")
		sys.exit(2)

	durations_us = [b[0] * 1e6 for b in bursts]
	counts = [b[1] for b in bursts]

	print(f"\nHREF pulses: {len(bursts)}")
	print(f"HREF duration (us):  avg={statistics.mean(durations_us):7.2f}  "
		  f"min={min(durations_us):7.2f}  max={max(durations_us):7.2f}")
	print(f"PCLK per HREF:       avg={statistics.mean(counts):7.1f}  "
		  f"min={min(counts)}  max={max(counts)}")

	if vsync_periods:
		vp_ms = [v * 1000 for v in vsync_periods]
		print(f"\nVSYNC periods captured: {len(vp_ms)}")
		print(f"Frame period (ms):   avg={statistics.mean(vp_ms):7.2f}  "
			  f"min={min(vp_ms):7.2f}  max={max(vp_ms):7.2f}")
		print(f"→ implied frame rate: {1000/statistics.mean(vp_ms):.2f} fps")
		if bursts and len(bursts) >= 2 and len(vsync_periods) >= 1:
			# Count HREFs between consecutive VSYNCs (approximate)
			avg_frame_s = statistics.mean(vsync_periods)
			href_rate = len(bursts) / (total / (args.sample_rate * 1e6))
			hrefs_per_frame = href_rate * avg_frame_s
			print(f"→ HREFs per frame:   ≈{hrefs_per_frame:.0f} "
				  f"(VGA expects 480, UXGA expects 1200)")

	print(f"\nInterpretation: {interpret(statistics.mean(counts))}")


if __name__ == "__main__":
	main()
