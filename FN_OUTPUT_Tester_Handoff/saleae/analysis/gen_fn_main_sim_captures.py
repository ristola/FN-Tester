import csv
import os
import re

# Real PCB-085 captures used as the FN Main screen's "Simulate" playback
# data (M5AtomS3-FN-Bridge/src/main.cpp cycles through these while
# simulation is armed). Chosen for evidence quality and a spread of
# distinct decodable states, not for demo variety - the same
# blind-validation capture from the first version of this feature is kept
# as one of them. All live outside this repo (see the feature's notes) -
# regenerate from the same folder if they move.
#
# The "PB & HTR Output 4&8" filename matters: docs/PCB085_ANALYSIS.md
# section 18 flags an older "RB & HTR Output 4&8.csv" as a mislabeled
# duplicate of the Output 5&6 capture - only the corrected filename below
# is real Output 4+8 evidence.
#
# Output format: previously a compiled-in C++ header
# (M5AtomS3-FN-Bridge/src/fn_main_sim_captures.h, ~330KB baked into every
# firmware image) - now a LittleFS data directory instead
# (M5AtomS3-FN-Bridge/data/sim_captures/), flashed onto the pod's
# filesystem partition separately via `pio run -e m5stack-atoms3 -t
# uploadfs`, not compiled into the firmware binary. manifest.txt lists one
# label per line (line number = capture index); <index>.bin holds that
# capture's edges as fixed 3-byte records (uint16 durationUs, little-
# endian, + uint8 level) - deliberately not a raw struct dump, so the file
# format doesn't depend on compiler struct packing.
CAPTURES_DIR = os.path.expanduser("~/Documents/Saleae Data Logger/FN-Main Board Captures")
CAPTURES = [
    ("No Outputs", "FN-Main No Outputs.csv"),
    ("Alarm Output 1", "FN-Main Alarm Output 1.csv"),
    ("PB & HTR Output 4&8", "FN-Main PB & HTR Output 4&8.csv"),
    ("Blind Validation", "FN-Main U Tell Me V1PBISOGAS.csv"),  # labels feed SM_FnMainStatusPayload.captureLabel[24] - keep each under 23 chars
    ("100 Percent Analog", "FN-Main - 100 Percent.csv"),
]

_here = os.path.dirname(os.path.abspath(__file__))
# M5AtomS3-FN-Bridge/ lives inside this same repo now (moved in when both
# projects merged into one FN-Tester monorepo), not as a sibling directory.
out_dir = os.path.join(_here, "..", "..", "..", "M5AtomS3-FN-Bridge", "data", "sim_captures")


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


os.makedirs(out_dir, exist_ok=True)
# Clean out any stale numbered files from a previous run with a different
# capture count, so a shrink doesn't leave an orphaned N.bin behind.
for name in os.listdir(out_dir):
    if re.fullmatch(r"\d+\.bin", name):
        os.remove(os.path.join(out_dir, name))

manifest_lines = []
for index, (label, filename) in enumerate(CAPTURES):
    assert len(label) < 24, f"{label!r} is too long for SM_FnMainStatusPayload.captureLabel[24]"
    path = os.path.join(CAPTURES_DIR, filename)
    edges = load_edges(path)
    print(f"{label!r}: {len(edges)} edges, {sum(d for _, d in edges) / 1000.0:.1f}ms")

    bin_path = os.path.join(out_dir, f"{index}.bin")
    with open(bin_path, "wb") as f:
        for lvl, d in edges:
            f.write(d.to_bytes(2, "little") + bytes([lvl]))
    manifest_lines.append(label)

manifest_path = os.path.join(out_dir, "manifest.txt")
with open(manifest_path, "w") as f:
    f.write("\n".join(manifest_lines) + "\n")

print(f"Wrote {len(CAPTURES)} capture(s) + manifest to {out_dir}")
print("Run `pio run -e m5stack-atoms3 -t uploadfs` (from M5AtomS3-FN-Bridge/) to flash them onto the pod.")
