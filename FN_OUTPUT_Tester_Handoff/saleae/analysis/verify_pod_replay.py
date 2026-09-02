"""
Verify the M5AtomS3-FN-Bridge pod's GPIO2 replay output against the known
reference frame it's supposed to be transmitting (fn_reference_frame_derived.csv,
the 386-edge filtered frame actually loaded onto the RMT hardware - see
fn_bus_tx.cpp / EXPERIMENT_LOG.md Experiment 002's addendum). This is the
"not yet done" visual verification flagged in that experiment: does the
replayed waveform, captured with a logic analyzer directly on GPIO2, actually
match the frame the firmware was asked to loop?

Input: captures/digital 2.csv - a single-channel Saleae digital export
(header "Time [s],Channel 1") of the pod's GPIO2 output.
"""
import csv
import os
from collections import Counter

import numpy as np

_here = os.path.dirname(os.path.abspath(__file__))
capture_path = os.path.join(_here, "..", "..", "captures", "digital 2.csv")
ref_path = os.path.join(_here, "..", "..", "captures", "fn_reference_frame_derived.csv")

rows = []
with open(capture_path, newline="") as f:
    r = csv.reader(f)
    next(r)
    for row in r:
        rows.append((float(row[0]), int(row[1])))

rows = [(t, lvl) for t, lvl in rows if t >= 0]
clean = []
prev = None
for t, lvl in rows:
    if lvl != prev:
        clean.append([t, lvl])
        prev = lvl
raw_edges = []
for i in range(len(clean) - 1):
    t0, lvl = clean[i]
    t1 = clean[i + 1][0]
    raw_edges.append([t0, lvl, (t1 - t0) * 1e6])  # start_s, level, duration_us

n_short = sum(1 for _, _, d in raw_edges if d < 3.2)
print(f"Raw capture: {len(raw_edges)} edges, {n_short} ({100*n_short/len(raw_edges):.1f}%) "
      f"shorter than 3.2us (noise-filter threshold)")

durs = Counter(round(d) for _, _, d in raw_edges if d < 300)
print("Top 10 raw pulse-width clusters (us:count):")
for d, c in durs.most_common(10):
    print(f"  {d:5d}us : {c}")

# --- Noise-filter: merge any edge shorter than 3.2us into its two neighbors,
# same threshold/rationale as extract_reference_frame.py used when building
# the RMT reference frame in the first place - apply it here too so we're
# comparing like with like instead of letting glitches dominate the signal.
def merge_short_edges(edges, threshold_us):
    edges = [e[:] for e in edges]
    changed = True
    while changed:
        changed = False
        for i in range(1, len(edges) - 1):
            lvl, dur = edges[i][1], edges[i][2]
            if dur < threshold_us:
                edges[i - 1][2] += dur + edges[i + 1][2]
                del edges[i:i + 2]
                changed = True
                break
    return edges


filtered = merge_short_edges(raw_edges, 3.2)
print(f"\nAfter merging edges < 3.2us: {len(filtered)} edges "
      f"(reference frame has 386)")

# --- Autocorrelation over a WIDE period search range (the earlier pass
# clipped its peak right at a 40ms search-window boundary - a sign the true
# peak was being cut off, not genuinely found).
t_start = filtered[0][0]
t_end = filtered[-1][0]
grid_us = 2.0
n = int((t_end - t_start) * 1e6 / grid_us)
signal = np.zeros(n, dtype=np.float32)
for t0, lvl, dur in filtered:
    if lvl != 1:
        continue
    i0 = int((t0 - t_start) * 1e6 / grid_us)
    i1 = int((t0 - t_start) * 1e6 / grid_us + dur / grid_us)
    signal[max(0, i0):min(n, i1)] = 1.0

signal -= signal.mean()
autocorr = np.correlate(signal, signal, mode="full")[n - 1:]
autocorr /= autocorr[0]

search_lo_us, search_hi_us = 2000, 200000  # 2ms - 200ms, wide open
lo_i, hi_i = int(search_lo_us / grid_us), min(int(search_hi_us / grid_us), len(autocorr))
peak_i = lo_i + int(np.argmax(autocorr[lo_i:hi_i]))
peak_period_us = peak_i * grid_us
print(f"\nAutocorrelation peak period (wide search): {peak_period_us/1000:.3f}ms "
      f"(acf={autocorr[peak_i]:.3f})")

# Also show the top 5 peaks so a harmonic/sub-harmonic mismatch is visible.
order = np.argsort(autocorr[lo_i:hi_i])[::-1][:5]
print("Top 5 autocorrelation peaks in [2ms, 200ms]:")
for idx in order:
    i = lo_i + idx
    print(f"  {i*grid_us/1000:.3f}ms  acf={autocorr[i]:.3f}")

# --- Load the reference frame the firmware was asked to transmit.
ref_edges = []
with open(ref_path, newline="") as f:
    r = csv.reader(f)
    next(r)
    for row in r:
        ref_edges.append((int(row[0]), float(row[1])))
ref_total_us = sum(d for _, d in ref_edges)
print(f"\nReference (fn_reference_frame_derived.csv): {len(ref_edges)} edges, "
      f"{ref_total_us:.1f}us span, starting level={ref_edges[0][0]}")

print("\nFirst 10 filtered capture edges vs first 10 reference edges:")
cum_ref = 0.0
for i in range(10):
    cap_lvl, cap_dur = filtered[i][1], filtered[i][2]
    ref_lvl, ref_dur = ref_edges[i]
    print(f"  capture: level={cap_lvl} dur={cap_dur:7.2f}us   |   "
          f"reference: level={ref_lvl} dur={ref_dur:7.2f}us")
