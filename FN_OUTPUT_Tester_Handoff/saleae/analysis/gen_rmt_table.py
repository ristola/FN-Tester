import csv
import os

_here = os.path.dirname(os.path.abspath(__file__))
in_path = os.path.join(_here, "..", "..", "captures", "fn_reference_frame_derived.csv")
out_path = os.path.join(_here, "..", "..", "..", "..", "M5AtomS3-FN-Bridge", "src", "fn_reference_frame.h")

rows = []
with open(in_path, newline="") as f:
    r = csv.reader(f)
    next(r)
    for row in r:
        rows.append((int(row[0]), float(row[1])))

assert len(rows) % 2 == 0, f"odd edge count {len(rows)}, RMT pairing needs even"
assert rows[0][0] == 1, "expected frame to start HIGH"

# Round each duration to whole microseconds (1us RMT tick), clamp to [1,32767]
# (RMT's 15-bit duration field; duration=0 means "end of transmission" so must
# never appear except as a true terminator - clamp minimum to 1 tick instead).
ticks = []
for lvl, dur_us in rows:
    t = round(dur_us)
    if t < 1:
        t = 1
    if t > 32767:
        t = 32767  # none of our data hits this (max ~8598us) but guard anyway
    ticks.append((lvl, t))

print(f"{len(ticks)} edges -> {len(ticks)//2} rmt_data_t words")
total_us = sum(t for _, t in ticks)
print(f"Total duration after rounding: {total_us}us (original ~50000us)")

with open(out_path, "w") as f:
    f.write("// Auto-generated from FN_OUTPUT_Tester_Handoff/captures/digital.csv - see\n")
    f.write("// docs/EXPERIMENT_LOG.md Experiment 002 and docs/FN_PROTOCOL_FINDINGS.md's\n")
    f.write("// \"Corrected frame period + frame-content finding\" section for how this\n")
    f.write("// was derived (autocorrelation + cross-correlation validated 50ms period,\n")
    f.write("// static/repeating content). One 1us-tick RMT-ready copy of a single clean\n")
    f.write("// 50.0ms period, starting HIGH, extracted from t=176-226ms into the capture.\n")
    f.write("// Regenerate with FN_OUTPUT_Tester_Handoff/saleae/analysis/gen_rmt_table.py\n")
    f.write("// if the source frame changes.\n")
    f.write("//\n")
    f.write("// rmt_data_t is `struct { union { struct { duration0:15, level0:1,\n")
    f.write("// duration1:15, level1:1 }; uint32_t val; }; }` - an anonymous struct\n")
    f.write("// nested inside an anonymous union, which C++ (unlike C) does not support\n")
    f.write("// designated-initializer syntax for. Positional aggregate init instead:\n")
    f.write("// the outer braces are rmt_data_t's one field (the union), the inner\n")
    f.write("// braces are the union's first alternative (the bit-field struct) -\n")
    f.write("// order is {duration0, level0, duration1, level1}.\n")
    f.write("#pragma once\n\n")
    f.write('#include "esp32-hal-rmt.h"\n\n')
    f.write(f"constexpr size_t kFnReferenceFrameWords = {len(ticks)//2};\n")
    f.write("constexpr rmt_data_t kFnReferenceFrame[kFnReferenceFrameWords] = {\n")
    for i in range(0, len(ticks), 2):
        l0, d0 = ticks[i]
        l1, d1 = ticks[i + 1]
        f.write(f"    {{{{{d0}, {l0}, {d1}, {l1}}}}},\n")
    f.write("};\n")

print(f"Wrote {out_path}")
