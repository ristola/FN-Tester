# FN Experiment Log

Copy this section for each controlled experiment.

## Experiment 001
Date: 2026-08-27
Operator: (unrecorded)
Board: FN-MAIN + FN-OUTPUT board pair (board revision / model of each not yet recorded — presumed PCB-110 generation OUTPUT board pending confirmation)
Board revision: UNKNOWN
Firmware/version markings: UNKNOWN
Test equipment: Saleae logic analyzer, 2 digital channels
Capture filename: `captures/digital.csv`

### Goal
Capture the FN two-wire link between an FN-MAIN and an FN-OUTPUT board from power-up through link establishment.

### Connections / Probe Points
- Channel 0: raw two-wire FN field input (the FN-MAIN↔OUTPUT comms line itself).
- Channel 1: U7 pin 7, board-silkscreened "COMM OK" (LED drive) — a derived link-status signal, not raw link data. See `docs/PCB110_ANALYSIS.md` for an unresolved contradiction (pin 7 is GND on a standard 74HC14, which U7 is otherwise hypothesized to be).
- FO/Wire selector position: not recorded.

### Conditions
FN-MAIN and FN-OUTPUT board connected together on the bench; logic analyzer clipped onto the 2-wire link between them.

### Action Performed
Boards were powered up to establish communications; capture spans power-up through link lock (not steady-state idle or a deliberate output command).

### Measurements
See `docs/FN_PROTOCOL_FINDINGS.md` → "Capture: digital.csv" for full detail. Summary:
- Duration 1.905131 s, 38,861 recorded edges across both channels.
- Channel 0 shows a recurring ~16.6–17.3 ms cycle (near 60 Hz mains period) with two alternating sub-cycle types: a fixed ~15–17 µs "reference" pulse, and a ~205 µs "data" pulse whose preceding delay ramps smoothly from ~503 µs to ~899 µs over the capture.
- A repeating chirp-like burst of edges (period shrinking ~28 µs → ~13 µs) precedes the pulse in most cycles.
- Channel 1 ("COMM OK") stays at 0 for the first ~96.7 ms (~6 mains half-cycles), then goes active and tracks Channel 0 closely for the rest of the capture.

### Raw Observations
Full numeric detail lives in `docs/FN_PROTOCOL_FINDINGS.md` rather than duplicated here, to keep one place of record.

### Interpretation
Channel 1's late activation is read as the receiver's link-lock/"COMM OK" indicator asserting once enough valid cycles have been seen on Channel 0 (~96.7 ms lock-in time). The Channel 0 cyclical structure looks mains-referenced, consistent with PCB-110's solid-state outputs needing zero-cross timing, but this capture is a power-up transient, not steady-state traffic — the ramping "data" pulse delay may reflect link/timing acquisition rather than a commanded dimmer level.

### Confidence
STRONG EVIDENCE for the timing structure as measured; HYPOTHESIS for what it means; UNKNOWN for the U7 pin 7 contradiction and for whether the ramp reflects protocol data vs. acquisition transient.

### Follow-up
1. Record board revision/firmware markings and FO/Wire selector position for this pair.
2. Resolve the U7 pin 7 contradiction (photo/continuity check).
3. Capture a second sample: steady-state (post-lock) traffic with no output change, to isolate power-up transient behavior from steady-state protocol.
4. Capture a third sample: a single deliberate output on/off and a deliberate fade, each with Channel 0 alone, to correlate the "data" pulse ramp with a known commanded level.

## Experiment 002 (re-analysis, not a new capture)
Date: 2026-08-27 (later session)
Operator: (unrecorded)
Board: same FN-MAIN + FN-OUTPUT pair as Experiment 001
Board revision: UNKNOWN (unchanged from Experiment 001)
Firmware/version markings: UNKNOWN
Test equipment: none new — re-analysis of `captures/digital.csv` from Experiment 001
Capture filename: `captures/digital.csv` (same file, no new capture taken)

### Goal
Determine the true frame period and whether this capture's content is static or varying, to decide whether it can be safely replayed as a keepalive/idle emulation without a full bit-level decode. Driven by a practical constraint: no new capture is currently obtainable (operator has no way to command an output change right now).

### Connections / Probe Points
Unchanged from Experiment 001 - no new hardware session, pure software re-analysis.

### Conditions
Operator reported (not independently re-verified this session): replaying this exact capture toward a real FN-OUTPUT board satisfies its watchdog and clears its alarm output - i.e. the board's own receiver accepts this as valid traffic.

### Action Performed
Autocorrelation (FFT-based, 2µs-resampled Channel 0, 200-600ms window) to find the true repeat period objectively, followed by cross-correlation re-synchronization of 11 consecutive ~48ms frame windows (200-750ms) against a reference frame to test whether frame content repeats or varies. Analysis scripts were ad-hoc Python (numpy in a scratch venv), not yet committed to `saleae/fn_decoder/`.

### Measurements
- True fundamental repeat period: 49.98-50.01ms (autocorrelation peak acf≈0.83), far stronger than the ~16.7ms period from Experiment 001 (acf≈0.47 at that lag). 50ms = exactly 3× the 60Hz mains period.
- After cross-correlation re-sync, 11 consecutive frames matched a reference frame at 93-97% sample-level agreement.
- Extracted a clean single-period reference frame: 610 Channel-0 edges, exactly 50.0ms span, starting at t=176ms into the capture (well past the ~96.7ms link-lock point).
- Caught and fixed a methodology bug from earlier ad-hoc analysis this same session: `digital.csv` emits a new CSV row on *either* channel's transition, so treating every row as a Channel-0 edge fragments true Channel-0 pulses whenever Channel 1 alone toggles mid-pulse. Corrected by merging consecutive same-level rows before any timing math.

### Raw Observations
Full detail in `docs/FN_PROTOCOL_FINDINGS.md` → "Corrected frame period + frame-content finding" section, including the reasoning for why this doesn't contradict Experiment 001's ramping-pulse-delay finding (that ramp lives in the capture's early acquisition window; this analysis started at t=200ms specifically to sample past it).

### Interpretation
This specific captured frame is very likely a fixed idle/keepalive frame (repeats near-identically every 50ms, and is independently confirmed by the operator to satisfy the real board's watchdog) rather than a snapshot of commanded, varying data. That makes it safe to extract and loop-replay for emulation purposes without first cracking the bit-level protocol - but it cannot teach us what a *different* commanded frame looks like.

### Confidence
STRONG EVIDENCE for the 50ms period and static frame content (both independently measured via standard signal-processing methods, not eyeballed). HYPOTHESIS that "static + watchdog-satisfying" means "idle/keepalive" semantically. UNKNOWN for bit-level encoding - unchanged, still requires a capture with a deliberate commanded change.

### Follow-up
1. When a new capture becomes possible: a deliberate single-output on/off change, Channel 0 only, to compare against this idle frame and start isolating which part of the 610-edge structure actually varies.
2. Build the pod-side (`M5AtomS3-FN-Bridge`) replay firmware using the extracted reference frame - bench-testable via GPIO/scope without touching the real 2-wire bus, since no isolation/protection circuitry exists yet (see `docs/TESTER_ARCHITECTURE.md`'s "Interface Safety" section - do not connect bare GPIO to the real bus). **Done, same session** - see below.
3. Formalize the ad-hoc analysis scripts from this session into `saleae/fn_decoder/` per the proposed layout in `docs/SALEAE_DECODER_NOTES.md`, rather than leaving them as scratch files. Partially done - scripts live in `saleae/analysis/` (not `saleae/fn_decoder/`, which is still the separate, not-yet-started Saleae extension itself).

### Addendum: pod-side replay implementation (same session)
Building the RMT-based replay (`M5AtomS3-FN-Bridge/src/fn_bus_tx.cpp`) surfaced a real hardware constraint that required revising the extracted reference frame: the ESP32-S3 has only 4 TX RMT channels x 64 words each (256 words max reachable by one channel object, confirmed via `rmtInit()` failing above that regardless of what else is using RMT), and true hardware auto-loop playback (`rmtLoop()`) requires the whole pattern to fit in that reserved memory. The unfiltered 610-edge/305-word frame doesn't fit. A software-refill approach (repeated one-shot `rmtWriteBlocking()` calls, meant to support arbitrarily large patterns via the driver's interrupt-based buffer refill) was tried first and crashes on real hardware (`E rmt: rmt_set_tx_thr_intr_en(508): RMT EVT THRESH ERR` followed by a `LoadProhibited` panic) - this Arduino-ESP32 core release (3.20017, IDF5-based) only ships the legacy `driver/rmt.h` API (confirmed absent in every other cached core version too: no `rmt_tx.h`/`rmt_rx.h`), and this specific code path appears broken in it for this chip.

Resolved by noise-filtering the reference frame before extraction: `extract_reference_frame.py` now merges any edge shorter than 3.2µs into its two (same-level) neighbors, on the reasoning that `docs/FN_PROTOCOL_FINDINGS.md` already flags sub-few-microsecond edges as ambiguous between real signal and comparator/ringing noise - explicitly a lossy simplification for the replay use case, not a claim that those edges are proven noise. This reduced the frame from 610 to 386 edges (193 RMT words, comfortably under 256), and `rmtLoop()` at that size has been confirmed stable on real hardware (start/stop/restart cycled repeatedly with no crash, ESP-NOW mesh functionality unaffected throughout).

**Not yet done:** visual verification that the pod's GPIO2 output actually matches the original capture waveform (needs the operator's Saleae connected to GPIO2 - nothing in this session confirmed the *replayed* waveform is correct beyond "the firmware runs without crashing and completes each loop in about the right total time"). Do this before trusting the replay for anything beyond a rough bench check.

## Experiment 003 (re-analysis, not a new capture)
Date: 2026-08-27 (later session)
Operator: (unrecorded)
Board: same FN-MAIN + FN-OUTPUT pair as Experiments 001/002
Board revision: UNKNOWN (unchanged)
Firmware/version markings: UNKNOWN
Test equipment: none new — re-analysis of `captures/digital.csv`, plus the MC145026/MC145027/MC145028 datasheet (Motorola/ON Semiconductor, REV 2, 1/98)
Capture filename: `captures/digital.csv` (same file, no new capture taken)

### Goal
Test the working hypothesis that FN-MAIN's transmit side is an MC145026-family trinary encoder — the natural counterpart to the two MC145027P decoder ICs CONFIRMED present on the PCB-110 OUTPUT board (`docs/PCB110_ANALYSIS.md`) — by comparing the datasheet's documented word format against the actual captured waveform, instead of assuming the connection.

### Connections / Probe Points
Unchanged from Experiment 001/002 — pure software re-analysis, no new hardware session.

### Conditions
Same as Experiment 002 (operator-reported watchdog/alarm-clearing behavior for this capture still stands, unaffected by this analysis).

### Action Performed
Obtained and read the full MC145026/27/28 datasheet. Wrote `saleae/analysis/compare_mc145026.py`, which re-extracts unfiltered (no short-edge merging) Channel-0-only edges over the same 176–226 ms single-frame window used in Experiment 002, then checks the datasheet's word-format requirements (9-trit word + mandatory 3-data-period gap = 12 data periods per continuous-mode repeat, all timed by one free-running RC oscillator unrelated to mains) against the capture's independently-established repeat structure (3 mains-locked ~16.667 ms sub-cycles per 50.0 ms, from Experiment 002's autocorrelation).

### Measurements
Full detail in `docs/FN_PROTOCOL_FINDINGS.md` → "MC145026 encoder-format comparison" section. Summary:
- Structural mismatch: captured repeat unit = 3 sub-cycles; MC145026 continuous-mode repeat unit = 12 data periods. Even the most charitable 1-sub-cycle-per-data-period mapping is short by 4x on the full repeat unit and 3x on a bare 9-trit word alone.
- Each ~16.7ms sub-cycle is dominated by two single-level dwells (43.9–84.9% of the sub-cycle), not a train of ~9 comparably-sized data periods as MC145026's Figure 11 word format would produce.
- Experiment 001's reported "~205µs data pulse" resolves, at full time resolution, into several distinct sub-edges (e.g. ~50/7/3.6/146µs in sequence) — the earlier single-pulse figure was a coarse/eyeballed grouping artifact, not a real single pulse.
- The fine-structure ("chirp") region contains genuinely repeating multi-edge motifs (not pure monotonic ringing) — e.g. a ~50/7/3.6/146µs pattern recurring 5+ times, then a ~27/1.8/26/7/3.6/141µs pattern recurring several times with a slow drift in one component. Not clustered/decoded in this pass.

### Raw Observations
Full numeric detail (script output) in `docs/FN_PROTOCOL_FINDINGS.md`; script itself at `saleae/analysis/compare_mc145026.py`, reproducible against `captures/digital.csv`.

### Interpretation
STRONG EVIDENCE that the raw two-wire waveform, as captured, is not a literal MC145026 `Dout` pin wired 1:1 onto the bus — the repeat-unit math is a clean structural argument that holds regardless of the encoder's unknown oscillator frequency. This does not rule out MC145026-style trinary/tri-level encoding being used at a finer timescale within the fine-structure regions (packed into a mains-synchronized slot rather than spread across a whole leisurely word), nor does it change the CONFIRMED fact that PCB-110's *receive* side uses MC145027 decoders — it only means that fact doesn't by itself establish the wire-level framing.

### Confidence
CONFIRMED for datasheet facts and raw capture measurements. STRONG EVIDENCE for the structural repeat-unit mismatch. HYPOTHESIS/UNKNOWN for whether MC145026-style symbols exist at a finer timescale within the fine-structure regions, and for what (if anything) sits between FN-MAIN and the MC145027 decoders to produce the observed mains-synchronous outer framing.

### Follow-up
1. Rigorous (non-eyeballed) clustering of fine-structure edge widths across multiple sub-cycles, to check for a small number of discrete symbol classes and a candidate oscillator period that would make a 9-trit-word count plausible.
2. Direct probe of FN-MAIN's transmit circuitry (TE/Dout or equivalent), if the board can be opened, to confirm or rule out MC145026 usage without inferring from the two-wire bus.
3. Investigate whether an intermediate modem/multiplexer stage between FN-MAIN and the MC145027 decoders would explain a mains-synchronous outer envelope wrapping MC145026-style inner symbols (ties to `docs/FN_PROTOCOL_FINDINGS.md` Open Question 8/15).

## Experiment ID (template — copy below for the next entry)
Date:
Operator:
Board:
Board revision:
Firmware/version markings:
Test equipment:
Capture filename:

### Goal

### Connections / Probe Points

### Conditions

### Action Performed

### Measurements

### Raw Observations

### Interpretation

### Confidence
CONFIRMED / STRONG EVIDENCE / HYPOTHESIS / UNKNOWN

### Follow-up
