import csv
import os

_here = os.path.dirname(os.path.abspath(__file__))
path = os.path.join(_here, "..", "..", "captures", "digital.csv")

rows = []
with open(path, newline="") as f:
    r = csv.reader(f)
    next(r)
    for row in r:
        rows.append((float(row[0]), int(row[1]), int(row[2])))

# Build TRUE ch0-only transitions: a new segment starts only when ch0's level
# actually changes vs. the previous row - a row where only ch1 toggled must be
# merged into the current ch0 segment (its start time is irrelevant to ch0).
ch0_true = []  # (start_time_s, level)
prev_level = None
for t, lvl, ch1 in rows:
    if lvl != prev_level:
        ch0_true.append([t, lvl])
        prev_level = lvl
# Now compute durations
ch0 = []
for i in range(len(ch0_true) - 1):
    t0, lvl = ch0_true[i]
    t1 = ch0_true[i + 1][0]
    ch0.append((t0, lvl, (t1 - t0) * 1e6))

print(f"True ch0 transitions: {len(ch0_true)} (was {len(rows)} raw rows)")

WIN_START_MS = 176.0
WIN_END_MS = 226.0  # full 50ms this time

frame = [[lvl, round(dur, 2)] for (t0, lvl, dur) in ch0 if WIN_START_MS <= t0 * 1000 < WIN_END_MS]

print(f"\nReference frame (unfiltered): {len(frame)} edges, span {WIN_END_MS - WIN_START_MS}ms")
print(f"Starting level: {frame[0][0]}")
total_us = sum(d for _, d in frame)
print(f"Sum of durations: {total_us:.2f}us (window={((WIN_END_MS-WIN_START_MS)*1000):.0f}us)")

bad = sum(1 for i in range(1, len(frame)) if frame[i][0] == frame[i-1][0])
print(f"Non-alternating adjacent levels: {bad} (should be 0)")

# Noise-filter to fit the ESP32-S3 pod's hard RMT hardware-loop limit (256
# words = 512 edges - see M5AtomS3-FN-Bridge/src/fn_bus_tx.cpp's comment on
# why: only 4 TX RMT channels x 64 words each on this chip, and the legacy
# RMT driver crashes rather than correctly streaming transmissions larger
# than that in the modes this project needs). Drop any edge shorter than
# FILTER_THRESHOLD_US by merging its duration into the next edge, i.e.
# treat a too-short blip as if the signal just continued at its prior level
# a bit longer - defensible for a REPLAY use case since docs/FN_PROTOCOL_
# FINDINGS.md already flags sub-microsecond/few-microsecond edges as
# ambiguous between real signal and comparator/ringing noise, not because
# we've proven they ARE noise. This is a lossy simplification, not a
# bit-exact copy - document it as such wherever this file is used.
FILTER_THRESHOLD_US = 3.2


def merge_short_edges(edges, threshold_us):
    # Dropping a short edge at index i without also merging its two
    # (same-level, since the sequence strictly alternates) neighbors at
    # i-1 and i+1 breaks alternation - e.g. dropping a short LOW between
    # two HIGHs would otherwise leave two adjacent HIGH entries instead of
    # one combined one. Fold all three into a single edge at the
    # neighbors' shared level instead: the short blip "didn't happen," so
    # the signal is treated as having stayed level i-1's level the whole
    # time through i+1.
    edges = [e[:] for e in edges]
    changed = True
    while changed:
        changed = False
        for i in range(1, len(edges) - 1):
            lvl, dur = edges[i]
            if dur < threshold_us:
                merged_dur = edges[i - 1][1] + dur + edges[i + 1][1]
                edges[i - 1][1] = merged_dur
                del edges[i:i + 2]  # drop the short edge and its now-absorbed neighbor
                changed = True
                break
    return edges


frame = merge_short_edges(frame, FILTER_THRESHOLD_US)
print(f"\nAfter merging edges < {FILTER_THRESHOLD_US}us: {len(frame)} edges "
      f"({len(frame)//2} RMT words, hardware limit is 256)")
bad = sum(1 for i in range(1, len(frame)) if frame[i][0] == frame[i-1][0])
print(f"Non-alternating adjacent levels after filtering: {bad} (should be 0)")

out_path = os.path.join(_here, "..", "..", "captures", "fn_reference_frame_derived.csv")
with open(out_path, "w") as f:
    f.write("level,duration_us\n")
    for lvl, dur in frame:
        f.write(f"{lvl},{dur}\n")
print(f"Wrote {out_path}")

print("\nFirst 15 edges:")
for lvl, dur in frame[:15]:
    print(f"  level={lvl} dur={dur}us")
print("Last 15 edges:")
for lvl, dur in frame[-15:]:
    print(f"  level={lvl} dur={dur}us")

# Redo the pulse-width histogram on the CORRECTED ch0 to see if the earlier
# clusters (205us, 7-8us, etc.) hold up
from collections import Counter
durs = [round(d) for (_, _, d) in ch0 if d < 300]
print("\nTop 20 corrected pulse-width clusters (us:count), full capture:")
for d, c in Counter(durs).most_common(20):
    print(f"  {d:5d}us : {c}")
