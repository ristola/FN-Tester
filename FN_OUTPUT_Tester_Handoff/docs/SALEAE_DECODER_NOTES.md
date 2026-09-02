# Saleae FN Decoder Notes

## Status (2026-08-31, updated)
`saleae/fn_decoder/` is a C++ Saleae Analyzer SDK plugin (Low Level Analyzer), not a Python
HighLevelAnalyzer -- the "Proposed Source Layout" below predates that choice and is now
stale on that point (kept for the layer breakdown, which still applies). It builds cleanly
(`cmake -B build && cmake --build build`, confirmed against the SDK fetched via
`cmake/ExternalAnalyzerSDK.cmake`) and now implements **all five layers**, selectable via a
new "Decode layer" setting (`FnDecodeMode` in `FnSymbolAnalyzerSettings.h`):

- **Raw dwell bins (layer 2, the original/default mode):** `FnSymbolAnalyzer` classifies
  every dwell (time between two edges) on one channel into one of five duration bins
  (`FnSymbolClass` in `FnSymbolAnalyzerResults.h`), using thresholds in
  `FnSymbolAnalyzerSettings` whose defaults trace directly to `docs/FN_PROTOCOL_FINDINGS.md`'s
  measurements. Bubble/tabular/export text show level (H/L) + bin letter, growing to full
  duration + bin name as bubble width allows. `FnSymbolSimulationDataGenerator` produces an
  illustrative waveform (cycles through all five bins, sized off the current settings) purely
  to preview bubble rendering without hardware -- it is explicitly not a claim about the real
  FN waveform's shape.
- **FN symbols + frames (layers 3-5, PCB-085):** `FnSymbolAnalyzer::WorkerThreadDecodeSymbols`
  classifies each interval as short/long/sync per `mFnShortLongSplitUs`/`mFnSyncMinUs`, groups
  4 intervals (3 for the field immediately before sync) into an `SLSL=0`/`LSLS=1` bit, collects
  9 bits (5 address + 4 data) into a word, and compares each word against its repeated-word
  pair. Recognized addresses are interpreted by the new `Pcb085Profile` module (kept separate
  from the common decode per CLAUDE.md's layering rule) into named outputs, analog code 0-15,
  and an approximate 4-20mA current. See "New in this update" below for exactly what this does
  and doesn't cover yet.

**Confirmed working against real hardware (2026-08-28):** loaded into Logic2 and run against
a real capture at the Channel 1 "COMM OK" onset region. The rendered bubbles reproduced the
already-documented structure almost exactly and with no new tooling-side surprises: an
`~8.3-8.6ms` LOW dwell (`LOW 8.388ms - dwell`) matching Experiment 001's figure, a dense
burst of alternating short/medium/noise-classed frames matching the "ringing/chirp burst"
finding in `docs/FN_PROTOCOL_FINDINGS.md`, and Channel 1's own brief pulses landing inside
that same chirp region as that doc's "occasional brief 100-300us pulses of its own
overlapping the Channel 0 chatter bursts" note describes. Levels alternated strictly
throughout (no same-level frame repeats), which is the basic sanity check for the
edge-to-edge WorkerThread logic. This is tool validation, not a new protocol finding --
it confirms the analyzer faithfully surfaces the same structure already hand-measured in
Python, nothing more.

**New in this update: layers 3-5 are now implemented in the C++ plugin.** The evidence this
is derived from: PCB-085 captures with known commanded output changes exist and have been
decoded down to a full symbol -> frame -> address+data -> board-profile pipeline, then
independently **blind-validated**: a capture was decoded to a predicted machine state
(`FN-Main U Tell Me V1PBISOGAS.csv` -> Valve 1 ON, Process Blower ON, Isolation Valve ON,
Process Heater ON, analog 0%) *before* the actual machine setup was revealed, and the two
matched exactly (`docs/PCB085_ANALYSIS.md` §10, §23). What's implemented, mapped to the rules
that justify it:

- **Layer 3 (symbol decoder) -- `FinishFnField()` in `FnSymbolAnalyzer.cpp`:** each interval
  classifies as short (S, ~25-27us) or long (L, ~180-182us) via `mFnShortLongSplitUs`; a
  4-interval field reads as `SLSL = 0` / `LSLS = 1` (bit value = whether interval 0 is long),
  with the field immediately before sync accepted truncated at 3 intervals (`SLS`/`LSL`)
  because the sync interval absorbs the final transition (`FN_PROTOCOL_FINDINGS.md` §7). A
  field whose intervals don't match the expected alternating shape still decodes
  best-effort from interval 0, but is flagged with a `kFnFrameError`/`kFnErrorMalformedSymbol`
  frame and the word's `kFrameFlagSymbolWarning` bit rather than silently trusted -- this
  mapping is STRONG EVIDENCE as a general rule, CONFIRMED for the specific values the blind
  validation test produced.
- **Layer 4 (frame detector) -- `WorkerThreadDecodeSymbols()`/`FinishFnWord()`:** any interval
  at/above `mFnSyncMinUs` (default 1000us) structurally ends the current word; a full word is
  9 fields (5 address + 4 data, `FN_PROTOCOL_FINDINGS.md` §8), matching the "35 intervals,
  sync, 35 intervals" burst shape from §6 (8 full 4-interval fields + 1 truncated 3-interval
  field = 35). Sync duration is also checked, informationally, against the ~1150-1500us band
  covering both observed ranges (~1.2645-1.2647ms / ~1.4195-1.4198ms, §4) and flagged via
  `kFrameFlagSyncKnownBand` if outside it -- per that section's "recognize a valid sync range
  rather than a single exact value" rule, this doesn't block the decode, only annotates it.
  Consecutive word copies are paired and compared; a mismatch sets `kFrameFlagSecondCopy`
  without `kFrameFlagCopiesMatch` rather than silently trusting one copy (§5). A sync arriving
  before 9 bits are collected emits `kFnFrameError`/`kFnErrorIncompleteWord` and drops the
  partial word rather than guessing.
- **Layer 5 (FN interpretation) -- new `Pcb085Profile.cpp`/`.h`:** each decoded word unpacks to
  `A1 A2 A3 A4 A5 | D1 D2 D3 D4`; kept as a separate module from the common decode per
  CLAUDE.md's "keep board interpretation separate from common FN decoding" rule, so a PCB-110
  profile can be added alongside it later rather than branching inside this one. Address
  `10001` -> outputs 1-4 (Alarm/Valve1/Valve2/Process Blower), `10010` -> outputs 5-8 (Regen
  Blower/Regen Heater/Isolation Valve/Process Heater) -- both CONFIRMED. `10110` -> 4-bit
  LSB-first analog code 0-15 plus an approximate requested percent and 4-20mA current
  (`current_mA = 4 + code * 16/15`, CONFIRMED/STRONG EVIDENCE for the code, a labeled *working
  model, not independently measured* for the mA figure per `PCB085_ANALYSIS.md` §13). `10100`
  is deliberately left undecoded (labeled "experimental") since its bit-level rule is STRONG
  EVIDENCE / NOT FULLY CONFIRMED (§14). `10000`/`10011` and any other address report raw
  address/data only with an UNKNOWN/unrecognized label -- outputs 9-16 are never guessed
  (§15). The raw address/data bit strings are always shown alongside the interpretation,
  never hidden behind only the named-output text (§21).

**Run against real hardware the same day (2026-08-31), and it found a real bug.** Loaded
into Logic 2 against a live PCB-085 capture with the "FN symbols + frames" decode mode: the
address/data decode read out cleanly as plausible FN words (`10000 0000`, `10100 1001`, ...),
which is itself corroborating evidence for the symbol-decode rule in Layer 3 above -- readable,
sensible-looking address/data strings falling out of real edge timings, not noise. But the
repeated-word pairing (Layer 4) was wrong: it alternated copy1/copy2 on every consecutive word
regardless of address, so word N (address `10000`) got compared against word N+1 (address
`10100`, a completely different address that merely followed it in the FN-MAIN address cycle)
and reported a nonsensical `MISMATCH`. **Fixed:** pairing is now keyed on the word's 5-bit
address -- a word is only compared against the immediately preceding word if that preceding
word's address matches; otherwise it's treated as a new address in the cycle (a fresh
"copy 1") rather than force-paired. Rebuilt clean (`cmake --build build`, no new warnings).
Not yet re-verified against that same capture to confirm the fix reads out `10100 1001 [copy1]`
then `10100 1001 [copy2 MATCH]` as expected -- do that before trusting mismatch flags in
general, since this is a same-day fix for a bug a human caught by eye, not yet independently
re-checked.

**Readability pass (same day):** the PCB-085 output-bank text (`Pcb085Profile::Interpret`,
addresses `10001`/`10010`) now lists only the outputs that are ON, one per line in the
tooltip/tabular/export text (`Valve 1 ON` / `Process Blower ON` / ...) instead of all 4
outputs with their OFF/ON state every time -- the raw `Data 0101` bit string is still shown
separately right before it, so nothing is hidden, just decluttered. The medium-zoom bubble
form (`FormatFnWordMedium` in `FnSymbolAnalyzerResults.cpp`) now also shows this same
ON-only summary (comma-separated) instead of only copy-pairing metadata, so useful
interpretation is visible without zooming all the way in to the tooltip.

**Known limitations of this pass:**
- Only PCB-085 is implemented; PCB-110 has no board-profile module yet (no commanded-output
  PCB-110 capture exists to build one from).
- The "known-good sync band" and noise-floor-ignore behavior in symbol/frame mode are new,
  narrower assumptions on top of the general findings above and haven't themselves been
  checked against a real capture -- treat them as STRONG EVIDENCE / tool-implementation
  choices, not independently confirmed protocol facts.
- The `4-20mA` figure only appears on frames whose decoded address is `10110` (the analog
  *value* address) -- addresses `10100` (analog companion, deliberately left as
  "experimental", no number) and `10000`/`10011` (unmapped output banks) never show it by
  design. A capture with no analog-sweep traffic in it simply has no `10110` word to show.

**Flagging a documentation gap:** `docs/EXPERIMENT_LOG.md` still ends at Experiment 003
(2026-08-27, the old PCB-110 power-up capture) and has no entries for any of the PCB-085
capture work behind the findings above (individual-output captures, the analog sweep, or the
blind validation test). Per this project's evidence rules that work should have its own
logged experiment(s) with capture filenames/setup, not just the summarized conclusions in
`PCB085_ANALYSIS.md`. Flagging rather than backfilling it myself, since I wasn't the one who
ran those captures and don't have the operator/setup details the log template requires.

## Objective
Develop a custom Saleae decoder for UNA-DYN/FN communications.

## Philosophy
Start with timing and raw symbols. Add semantic decoding only when proven.

## Layers
1. **Digital input**
2. **Edge/pulse analyzer** — timestamps, high/low duration, period, classification. *Implemented in `FnSymbolAnalyzer.cpp` (`kFnDecodeRawDwell` mode).*
3. **Symbol decoder** — SLSL/LSLS interval-quads → 0/1 (see Status above for confidence/caveats). *Implemented in `FnSymbolAnalyzer.cpp` (`kFnDecodeSymbolsAndFrames` mode), not yet run against real hardware.*
4. **Frame detector** — 71-interval burst = two 35-interval word copies + one sync gap; compare both copies. *Implemented, not yet run against real hardware.*
5. **FN interpretation** — 5-bit address + D1-D4 data → board profile (PCB-085 outputs 1-8 + analog confirmed; 9-16 unknown) → invalid frames. *Implemented in `Pcb085Profile.cpp`, not yet run against real hardware.*

## Useful Annotations
```text
FN RAW S0 S2 S1 ...
FN FRAME len=...
FN ADDR ...
FN CMD ...
OUT 7 ON
FRAME ERROR: timing
UNKNOWN FRAME: ...
```

## Possible Settings
- input polarity
- timing tolerance
- board profile
- raw-symbol display
- verbose timing
- auto-detect timing

## Controlled Capture Strategy
1. idle/baseline — done (PCB-085, `FN-Main No Outputs.csv`)
2. output 1 OFF → ON — done for outputs 1-8 (PCB-085, see `PCB085_ANALYSIS.md` §17 capture inventory)
3. output 1 ON → OFF — not distinguished from (2) in the current capture set; treated as symmetric
4. repeat every output — done for outputs 1-8; outputs 9-16 still pending (`PCB085_ANALYSIS.md` §15, §22)
5. change board address if possible — done (analog sweep 0/10/25/50/60/75/100% exercises address `10110`/`10100`, `PCB085_ANALYSIS.md` §11-14)
6. multiple boards — not done; only one PCB-085 unit captured so far
7. compare PCB-110 with newer 16-output board — not done; PCB-110 evidence is still limited to the Experiment 001 power-up capture, no commanded-output PCB-110 capture exists yet

## Proposed Source Layout
```text
saleae/fn_decoder/
├── HighLevelAnalyzer.py
├── extension.json
├── README.md
└── tests/
```

`digital.csv` is the first capture intended for analysis. Preserve the original.
