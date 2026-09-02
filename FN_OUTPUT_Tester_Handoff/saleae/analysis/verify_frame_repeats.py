import csv
import numpy as np

path = "/Users/rts/Development/CYD-4.3-FN-Tester/FN_OUTPUT_Tester_Handoff/captures/digital.csv"

rows = []
with open(path, newline="") as f:
    r = csv.reader(f)
    next(r)
    for row in r:
        rows.append((float(row[0]), int(row[1]), int(row[2])))

SR_US = 1.0  # 1us resolution for precision
T0 = 0.0
T_END = rows[-1][0]
N_TOTAL = int((T_END - T0) * 1e6 / SR_US)

full = np.zeros(N_TOTAL, dtype=np.int8)
for i in range(len(rows) - 1):
    t, lvl, ch1 = rows[i]
    t_next = rows[i + 1][0]
    start_i = int((t - T0) * 1e6 / SR_US)
    end_i = int((t_next - T0) * 1e6 / SR_US)
    start_i = max(0, start_i)
    end_i = min(N_TOTAL, end_i)
    if end_i > start_i:
        full[start_i:end_i] = lvl

def window(center_ms, half_width_us):
    c = int(center_ms * 1000 / SR_US)
    hw = int(half_width_us / SR_US)
    return full[c - hw: c + hw]

PERIOD_MS = 50.0
START_MS = 200.0
N_WORDS = 12
WORD_HALF_US = 24000  # 48ms window (a bit less than 50ms to allow search slack)
SEARCH_US = 300  # search +/- 300us around nominal k*50ms for best alignment

ref = window(START_MS, WORD_HALF_US)

print("Cross-correlation re-sync results (word k vs word 0):")
for k in range(1, N_WORDS):
    nominal_center = START_MS + k * PERIOD_MS
    best_shift = None
    best_score = -1
    search_range = range(-SEARCH_US, SEARCH_US + 1, 1)
    # coarse search in raw sample steps for speed
    cand = window(nominal_center, WORD_HALF_US + SEARCH_US)
    # slide ref over cand and find best match position (normalized agreement fraction)
    L = len(ref)
    best_off = None
    best_frac = -1
    step = 2  # search step in samples (us) for speed
    for off in range(0, 2 * SEARCH_US, step):
        seg = cand[off: off + L]
        if len(seg) != L:
            continue
        frac = np.mean(seg == ref)
        if frac > best_frac:
            best_frac = frac
            best_off = off
    shift_us = best_off - SEARCH_US
    print(f"  word{k}: nominal_center={nominal_center:.3f}ms  best_shift={shift_us:+d}us  "
          f"match_fraction={best_frac:.4f}  implied_period={PERIOD_MS + shift_us/1000:.4f}ms")
