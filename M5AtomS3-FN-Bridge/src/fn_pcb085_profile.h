#pragma once

#include <cstddef>
#include <cstdint>

#include "esp32-hal-rmt.h"

#include "espnow_protocol.h"
#include "fn_symbol_codec.h"
#include "fn_word_decoder.h"

// PCB-085 board profile - the ONLY place that knows what a PCB-085
// address/data word means, per FN_OUTPUT_Tester_Handoff/CLAUDE.md's
// layering rule ("the board profile should answer: what does that
// address/data mean for PCB-085"). Builds a full transmission cycle from
// live tracked output/analog state (main.cpp's s_fn_output_state /
// s_fn_analog_percent), using only fn_symbol_codec.h's confirmed bit/word
// timing - this file owns address assignment and data-bit ordering only.
//
// Deliberately independent of the CYD's SD-persisted/operator-editable
// "Address Banks" profile (fn_bank_profile.h over there) - that profile is
// a discovery/notebook tool an operator can add unconfirmed entries to,
// and this pod has no SD card to read it from anyway. What actually drives
// real transmitted hardware traffic is only the CONFIRMED constants below,
// taken directly from docs/PCB085_ANALYSIS.md.
//
// NOT YET HARDWARE-VALIDATED - see fn_symbol_codec.h's header comment.

constexpr int kFnPcb085MaxOutputs = 16; // matches main.cpp's kMaxFnOutputs

// Addresses encoded per cycle: 10001 (outputs 1-4, CONFIRMED), 10010
// (outputs 5-8, CONFIRMED), 10110 (analog code 0-15, CONFIRMED), 10100
// (analog "companion", STRONG EVIDENCE / NOT FULLY CONFIRMED per
// docs/PCB085_ANALYSIS.md section 14 - transmitted anyway, using that
// section's documented formula, but its value should be treated as
// experimental, not as verified protocol behavior, until an even analog
// code capture validates it). Outputs 9-16 (candidate addresses
// 10000/10011) are UNKNOWN and are never encoded - see that doc's section
// 15, "do not assign them yet."
constexpr size_t kFnPcb085AddressCount = 4;
constexpr size_t kFnPcb085MaxWords = kFnPcb085AddressCount * kFnWordsPerAddressBlock;

// Builds one full cycle into `out` (capacity `outCapacity` rmt_data_t
// words, expected to be at least kFnPcb085MaxWords). outputs[0..7] are
// Output 1..8's requested state (entries 8-15 are read but currently
// unused - no confirmed encoding exists for outputs 9-16 yet).
// analogPercent is clamped to 0-100 and mapped with the confirmed
// code = floor(percent * 15 / 100) rule. Returns the number of words
// written, or 0 on overflow.
size_t fn_pcb085_profile_build_cycle(const bool outputs[kFnPcb085MaxOutputs], uint8_t analogPercent,
                                      rmt_data_t *out, size_t outCapacity);

// Decode-direction inverse of fn_pcb085_profile_build_cycle(): given one
// word decoded by fn_word_decoder.h (address+data bits, not yet
// interpreted), updates outputs[]/*analogCode in place for the two
// confirmed digital banks (10001->Outputs 1-4, 10010->Outputs 5-8) and the
// confirmed analog bank (10110, LSB-first - matching the encode direction
// above). 10100 (analog companion, still experimental) and the UNKNOWN
// candidate banks 10000/10011 are recognized as a known PCB-085 address
// (returns FN_MAIN_PROFILE_PCB085) without exposing any further state -
// there's no confirmed meaning yet to surface for them. Returns
// FN_MAIN_PROFILE_UNRECOGNIZED for any other 5-bit address.
FnMainProfileMatch fn_pcb085_profile_apply_decoded_word(const FnDecodedWord &word,
                                                          bool outputs[kFnPcb085MaxOutputs], uint8_t *analogCode);
