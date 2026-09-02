import csv
import os
import re

# Real PCB-085 captures used as the FN Main screen's "Simulate" playback
# data (M5AtomS3-FN-Bridge/src/main.cpp cycles through these on each pod
# button press while simulation is armed). Chosen for evidence quality and
# a spread of distinct decodable states, not for demo variety - the same
# blind-validation capture from the first version of this feature is kept
# as one of them. All live outside this repo (see the feature's notes) -
# regenerate from the same folder if they move.
#
# The "PB & HTR Output 4&8" filename matters: docs/PCB085_ANALYSIS.md
# section 18 flags an older "RB & HTR Output 4&8.csv" as a mislabeled
# duplicate of the Output 5&6 capture - only the corrected filename below
# is real Output 4+8 evidence.
CAPTURES_DIR = os.path.expanduser("~/Documents/Saleae Data Logger/FN-Main Board Captures")
CAPTURES = [
    ("No Outputs", "FN-Main No Outputs.csv"),
    ("Alarm Output 1", "FN-Main Alarm Output 1.csv"),
    ("PB & HTR Output 4&8", "FN-Main PB & HTR Output 4&8.csv"),
    ("Blind Validation", "FN-Main U Tell Me V1PBISOGAS.csv"),  # labels feed SM_FnMainStatusPayload.captureLabel[24] - keep each under 23 chars
    ("100 Percent Analog", "FN-Main - 100 Percent.csv"),
]

_here = os.path.dirname(os.path.abspath(__file__))
out_path = os.path.join(_here, "..", "..", "..", "..", "M5AtomS3-FN-Bridge", "src", "fn_main_sim_captures.h")


def sanitize_ident(label):
    return re.sub(r"[^A-Za-z0-9]+", "_", label).strip("_")


def load_edges(path):
    rows = []
    with open(path, newline="") as f:
        r = csv.reader(f)
        next(r)
        for row in r:
            rows.append((float(row[0]), int(row[1])))
    edges = []
    for i in range(len(rows) - 1):
        t0, lvl = rows[i]
        t1, _ = rows[i + 1]
        dur_us = (t1 - t0) * 1e6
        d = round(dur_us)
        if d < 1:
            d = 1
        assert d < 65535, f"{path}: duration {dur_us}us overflows uint16_t"
        edges.append((lvl, d))
    return edges


with open(out_path, "w") as f:
    f.write("// Auto-generated from real PCB-085 captures (see\n")
    f.write("// FN_OUTPUT_Tester_Handoff/saleae/analysis/gen_fn_main_sim_captures.py's\n")
    f.write("// header comment for exactly which ones and why) - NOT synthetic/plausible-\n")
    f.write("// looking waveforms. Feeds the FN Main screen's \"Simulate\" mode\n")
    f.write("// (main.cpp/fn_word_decoder.h) so the real-time decoder can be exercised\n")
    f.write("// and demonstrated without any FN-MAIN receive hardware existing yet - see\n")
    f.write("// FN_OUTPUT_Tester_Handoff/docs/TESTER_ARCHITECTURE.md's \"Interface Safety\"\n")
    f.write("// section for why real listening isn't safely buildable yet.\n")
    f.write("//\n")
    f.write("// Regenerate with gen_fn_main_sim_captures.py if the source captures change.\n")
    f.write("#pragma once\n\n")
    f.write("#include <cstddef>\n")
    f.write("#include <cstdint>\n\n")
    f.write("struct FnSimEdge\n{\n    uint16_t durationUs;\n    uint8_t level;\n};\n\n")
    f.write("struct FnSimCapture\n{\n    const char *label;\n    const FnSimEdge *edges;\n    size_t length;\n};\n\n")

    idents = []
    for label, filename in CAPTURES:
        path = os.path.join(CAPTURES_DIR, filename)
        edges = load_edges(path)
        ident = sanitize_ident(label)
        idents.append((label, ident, len(edges)))
        print(f"{label!r}: {len(edges)} edges, {sum(d for _, d in edges) / 1000.0:.1f}ms")

        f.write(f"// Source: \"{filename}\"\n")
        f.write(f"constexpr FnSimEdge kFnSimEdges_{ident}[{len(edges)}] = {{\n")
        for lvl, d in edges:
            f.write(f"    {{{d}, {lvl}}},\n")
        f.write("};\n\n")

    f.write(f"constexpr size_t kFnMainSimCaptureCount = {len(idents)};\n")
    f.write("constexpr FnSimCapture kFnMainSimCaptures[kFnMainSimCaptureCount] = {\n")
    for label, ident, length in idents:
        escaped = label.replace('"', '\\"')
        f.write(f'    {{"{escaped}", kFnSimEdges_{ident}, {length}}},\n')
    f.write("};\n")

print(f"Wrote {out_path}")
