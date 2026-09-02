#pragma once

#include <cstddef>
#include <cstdint>

#include "esp32-hal-rmt.h"

// Common FN symbol/word codec - encodes a 5-bit address + 4-bit data word
// into the RMT interval sequence documented in
// FN_OUTPUT_Tester_Handoff/docs/FN_PROTOCOL_FINDINGS.md ("Short/Long
// Symbol Representation", "Frame/Word Structure") and confirmed against
// PCB-085 captures in FN_OUTPUT_Tester_Handoff/docs/PCB085_ANALYSIS.md.
// Deliberately knows nothing about what any address/data value *means* -
// that's a board profile's job (fn_pcb085_profile.h) - per
// FN_OUTPUT_Tester_Handoff/CLAUDE.md's layering rule, keep this file free
// of PCB-085/PCB-110-specific address meanings.
//
// NOT YET HARDWARE-VALIDATED: this is a first-principles encoder built by
// running the documented decode rules in reverse. It has not been
// bench-verified (scope/logic analyzer) against a real capture or a real
// OUTPUT board. Before trusting its output, capture the pod's GPIO2 replay
// and compare it against docs/PCB085_ANALYSIS.md's worked example
// (address 10001 data 0101 / address 10010 data 0011 / address 10110 data
// 0000 for the blind-validation state).
//
// Deliberate simplification vs. the literal captured waveform: the real
// capture's final bit of each 9-bit copy reads as a truncated 3-interval
// field because the sync gap immediately follows it (see
// FN_PROTOCOL_FINDINGS.md section 7). This encoder does not replicate that
// truncation - it emits every bit as a full 4-interval S/L quad and then a
// separate sync interval - which keeps word-building parity-safe (every
// encoded address block is guaranteed an even total interval count, so no
// RMT rmt_data_t half ever needs zero-padding) without requiring an
// exact-edge-count match to the original capture. A duration-based decoder
// (this project's whole premise, since MC145027-family decoders don't
// count edges) should not care about this difference, but it's an
// assumption, not evidence - flag it if a real capture disagrees.

// SHORT/LONG interval timing, in RMT ticks (fn_bus_tx.cpp configures a
// 1us/tick RMT clock, so these are also microseconds). Midpoints of the
// ranges in FN_PROTOCOL_FINDINGS.md section 3 - STRONG EVIDENCE, not a
// single confirmed exact value.
constexpr uint16_t kFnShortUs = 26; // documented range: 25-27us
constexpr uint16_t kFnLongUs = 181; // documented range: 180-182us

// Sync / inter-word gap, in the same units. Two sync-like durations have
// been observed (~1264.5-1264.7us and ~1419.5-1419.8us) - which applies to
// which board/FN-MAIN generation is UNKNOWN (FN_PROTOCOL_FINDINGS.md
// section 4), so this is a working default, not a confirmed constant. Also
// reused as the gap between different addressed words in a full
// transmission cycle - that reuse is a HYPOTHESIS, not evidence: the real
// inter-word/address-cycle timing is explicitly unconfirmed
// (PCB085_ANALYSIS.md section 22 item 8, FN_PROTOCOL_FINDINGS.md section
// 12). Revisit both uses once a real multi-address capture's inter-word
// gaps have been measured.
constexpr uint16_t kFnSyncUs = 1265;

// RMT words (rmt_data_t, 2 intervals each) one fully-encoded address block
// takes: 9 bits * 4 intervals/bit = 36, *2 copies = 72, + 2 sync/gap
// intervals (mid-word + trailing) = 74 intervals = 37 words. Callers
// building a multi-address cycle should size their buffer as
// (number of addresses) * kFnWordsPerAddressBlock, capped by fn_bus_tx's
// RMT_MEM_256 hardware ceiling (256 words/channel).
constexpr size_t kFnWordsPerAddressBlock = 37;

// Encodes one address+data word as: copy 1 (address then data, 5+4 bits,
// MSB/A1-first) - sync - copy 2 (identical) - trailing gap (see this
// file's header comment for why both gaps use the same kFnSyncUs and why
// bits aren't truncated adjacent to them). addressBits[0]=A1 .. [4]=A5,
// dataBits[0]=D1 .. [3]=D4, matching the "10001 0101" left-to-right
// notation used throughout the project's docs - callers should NOT
// pre-reverse bit order for boards documented as "LSB-first" (e.g.
// PCB-085 address 10110); that reversal is board-profile-specific
// interpretation and belongs in fn_pcb085_profile.cpp, not here.
//
// *level carries the current signal level across calls (toggle-only
// encoding - MC145027-family decoders are edge/duration based, so the
// absolute starting level of the whole cycle doesn't matter, only that
// each call continues toggling from where the previous one left off).
// Initialize it to either value before the first call in a cycle.
//
// Returns the number of rmt_data_t words written (always
// kFnWordsPerAddressBlock on success), or 0 if outCapacity is too small.
size_t fn_symbol_codec_encode_word(const bool addressBits[5], const bool dataBits[4],
                                    rmt_data_t *out, size_t outCapacity, bool *level);
