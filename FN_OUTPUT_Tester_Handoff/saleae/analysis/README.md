# FN Capture Analysis Scripts

Ad-hoc Python analysis scripts used to re-analyze `../../captures/digital.csv` (2026-08-27 session) - see `docs/EXPERIMENT_LOG.md` Experiment 002 and `docs/FN_PROTOCOL_FINDINGS.md`'s "Corrected frame period + frame-content finding" section for the results and reasoning. These are not the planned `saleae/fn_decoder/` Saleae HighLevelAnalyzer extension (still not started) - just the scripts that produced the 50ms-period / static-frame findings and the extracted reference frame.

Need `numpy` (only `find_frame_period.py` and `verify_frame_repeats.py` use it, for FFT-based autocorrelation/cross-correlation) - a scratch venv works fine:

```sh
python3 -m venv venv && venv/bin/pip install numpy
```

## Scripts (run in this order to reproduce)

1. **`find_frame_period.py`** - FFT autocorrelation of a resampled Channel 0 waveform; finds the true fundamental repeat period (~50ms) objectively, rather than assuming the ~16.7ms mains-cycle period from Experiment 001.
2. **`verify_frame_repeats.py`** - cross-correlation re-synchronization of consecutive ~48ms frame windows against a reference frame; confirms frame content is static/repeating (93-97% match) rather than varying per frame.
3. **`extract_reference_frame.py`** - extracts one clean 50.0ms period as `../captures/fn_reference_frame_derived.csv`. Fixes a bug present in early ad-hoc analysis this session: `digital.csv` emits a row on *either* channel's transition, so naively treating every row as a Channel-0 edge fragments true Channel-0 pulses when Channel 1 alone toggles mid-pulse - this script merges consecutive same-level rows first. Then noise-filters: merges any edge shorter than `FILTER_THRESHOLD_US` (3.2µs) into its two same-level neighbors, reducing 610 edges to 386 (193 RMT words) - needed to fit the pod's hard RMT hardware-loop limit of 256 words (see `M5AtomS3-FN-Bridge/src/fn_bus_tx.cpp`'s comment for why). This is a lossy simplification, not a bit-exact copy - the findings doc already flags these short edges as ambiguous between real signal and comparator/ringing noise.
4. **`gen_rmt_table.py`** - converts the extracted (filtered) reference frame into `M5AtomS3-FN-Bridge/src/fn_reference_frame.h`, a `rmt_data_t` array (1µs ticks) for the pod's RMT-based bench-test replay feature.

## Important limitation

This all derives from a single power-up-transient capture with no known-varying comparison point - see the findings doc for why "static + watchdog-satisfying" is good enough to safely replay as an idle/keepalive frame, but does **not** establish bit-level protocol meaning. Re-run step 3-4 if a better capture (steady-state, or with a deliberate commanded change) becomes available.
