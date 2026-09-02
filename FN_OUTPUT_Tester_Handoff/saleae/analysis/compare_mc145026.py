"""
Compare the captured Channel-0 waveform (captures/digital.csv) against the
MC145026 encoder's documented word format (Motorola/ON MC145026/27/28
datasheet, REV 2 1/98), to test the working hypothesis that FN-MAIN's
transmit side is (or directly reuses) an MC145026-style trinary encoder --
the natural counterpart to the two MC145027 decoders confirmed present on
the PCB-110 OUTPUT board (see docs/PCB110_ANALYSIS.md).

Datasheet facts used (all directly from the datasheet text/formulas, not
assumed):
- One encoding sequence = word (9 trits) + 3-data-period silent gap + word
  (9 trits) [Operating Characteristics, "words are transmitted twice"].
- In continuous-transmission mode (TE held low), this 9-trit-word +
  3-data-period-gap unit (12 data periods) repeats indefinitely as long as
  TE stays low, per Figure 10.
- R1*C1 = 1.72 encoder clock periods (short/long pulse decision threshold).
- R2*C2 = 33.5 encoder clock periods = 4 data periods -> 1 data period =
  33.5/4 = 8.375 encoder oscillator clock periods (Tosc).
- Encoder oscillator is a free-running external RC oscillator (fosc =
  1/(2.3*Rtc*Ctc')), practical range ~1kHz-400kHz, with NO reference to AC
  mains anywhere in the device.

This script does not assume the hypothesis is true or false -- it pulls
concrete numbers from the actual capture and checks whether they are even
structurally compatible with that word format, independent of the unknown
Tosc.
"""
import csv
import os
from collections import Counter

_here = os.path.dirname(os.path.abspath(__file__))
path = os.path.join(_here, "..", "..", "captures", "digital.csv")

rows = []
with open(path, newline="") as f:
    r = csv.reader(f)
    next(r)
    for row in r:
        rows.append((float(row[0]), int(row[1]), int(row[2])))

# True Channel-0-only transitions (merge rows where only ch1 toggled) --
# same corrected methodology as extract_reference_frame.py /
# docs/EXPERIMENT_LOG.md Experiment 002.
ch0_true = []
prev_level = None
for t, lvl, ch1 in rows:
    if lvl != prev_level:
        ch0_true.append([t, lvl])
        prev_level = lvl

ch0 = []
for i in range(len(ch0_true) - 1):
    t0, lvl = ch0_true[i]
    t1 = ch0_true[i + 1][0]
    ch0.append((t0, lvl, (t1 - t0) * 1e6))  # start_s, level, duration_us

print(f"Total Ch0-only edges in capture: {len(ch0)}")

# --- 1. One full 50ms frame, UNFILTERED (no short-edge merging), so the
# chirp burst's true fine structure is preserved for this analysis (the
# fn_reference_frame_derived.csv on disk is the *filtered* RMT-replay
# version and is not suitable for this check).
WIN_START_MS = 176.0
WIN_END_MS = 226.0
frame = [(lvl, round(dur, 3)) for (t0, lvl, dur) in ch0 if WIN_START_MS <= t0 * 1000 < WIN_END_MS]
print(f"\nUnfiltered single-frame window {WIN_START_MS}-{WIN_END_MS}ms: {len(frame)} edges")

# --- 2. Split into the three ~16.7ms mains-cycle sub-cycles and describe
# the gross envelope of each (how much time is spent in big single-level
# dwells vs. fine structure).
cum = 0.0
sub_cycles = [[], [], []]
idx = 0
boundaries_us = [0, 16667, 33333, 50000]
for lvl, dur in frame:
    mid_us = cum + dur / 2
    for b in range(3):
        if boundaries_us[b] <= mid_us < boundaries_us[b + 1]:
            sub_cycles[b].append((lvl, dur))
            break
    cum += dur

for i, sc in enumerate(sub_cycles):
    total = sum(d for _, d in sc)
    big = [(lvl, d) for lvl, d in sc if d > 1000]  # >1ms dwells
    small = [(lvl, d) for lvl, d in sc if d <= 1000]
    big_time = sum(d for _, d in big)
    small_time = sum(d for _, d in small)
    print(f"\nSub-cycle {i+1}: {len(sc)} edges, span {total:.1f}us")
    print(f"  Big (>1ms) dwells: {len(big)} edges, {big_time:.1f}us ({100*big_time/total:.1f}% of sub-cycle)")
    for lvl, d in big:
        print(f"    level={lvl} dur={d/1000:.3f}ms")
    print(f"  Fine structure (<=1ms): {len(small)} edges, {small_time:.1f}us ({100*small_time/total:.1f}% of sub-cycle)")

# --- 3. Chirp burst period-trend check: is the burst a smooth monotonic
# sweep (ringing-like) or does it cluster into a small number of discrete
# widths (symbol-like)? Look at sub-cycle 3's fine structure (or whichever
# sub-cycle has the most small edges) edge-by-edge.
richest = max(range(3), key=lambda i: len([e for e in sub_cycles[i] if e[1] <= 1000]))
fine = [d for lvl, d in sub_cycles[richest] if d <= 1000]
print(f"\nChirp/fine-structure edge durations in sub-cycle {richest+1} (us), in time order:")
print("  " + ", ".join(f"{d:.2f}" for d in fine))
distinct_bins = len(set(round(d) for d in fine))
print(f"  {len(fine)} edges, {distinct_bins} distinct integer-us widths "
      f"(a discrete N-level symbol code should collapse to a small handful; "
      f"smoothly-varying ringing should not)")

# --- 4. Narrow-pulse ratio check: reference (~16us) vs data (~205us) pulse,
# against the datasheet's short:long ratio implied by R1*C1=1.72 clocks
# sitting near the middle of an 8.375-clock data period split roughly
# 30:70 (short ~2.5 clocks : long ~5.9 clocks =~ 1:2.4).
durs = [round(d, 2) for (_, _, d) in ch0 if d < 300]
print("\nTop pulse-width clusters (us:count) under 300us, full capture:")
for d, c in Counter(round(x) for x in durs).most_common(15):
    print(f"  {d:5d}us : {c}")

# --- 5. Structural period-count check: MC145026 continuous mode repeats
# every (9 trits + 3-period gap) = 12 data periods, all at ONE fixed Tosc
# set by the encoder's own RC network -- unrelated to any external
# reference. Compare that hard requirement against the observed repeat
# unit (3 mains-referenced sub-cycles per 50ms, independently established
# via autocorrelation in Experiment 002).
print("\n--- Structural fit check ---")
print("MC145026 continuous-mode repeat unit = 9 (word) + 3 (gap) = 12 data periods,")
print("all timed by ONE free-running RC oscillator (fosc), NOT mains-referenced.")
print("Captured frame's independently-measured repeat unit = 3 sub-cycles per 50.0ms")
print("(autocorrelation, Experiment 002), each sub-cycle mains-locked at ~16.667ms.")
print("Even mapping 1 sub-cycle <-> 1 data period (most charitable case),")
print("3 available slots vs 12 required (9 trits + mandatory 3-period silent gap):")
print("  -> short by a factor of 4x; a bare 9-trit word alone would need 9 slots,")
print("     already 3x more than the 3 sub-cycles observed per repeat.")
