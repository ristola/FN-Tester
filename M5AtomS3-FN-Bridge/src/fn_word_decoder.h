#pragma once

#include <cstdint>

// Real-time port of the validated FN symbol/word decoder in
// FN_OUTPUT_Tester_Handoff/saleae/fn_decoder/src/FnSymbolAnalyzer.cpp's
// WorkerThreadDecodeSymbols() (Saleae offline analyzer, batch-processes a
// whole capture) to a one-interval-at-a-time state machine (this pod has no
// batch capture to hand it, only a live or simulated edge stream). Mirrors
// that file's thresholds and state transitions 1:1 rather than
// re-deriving them - see FN_PROTOCOL_FINDINGS.md sections 3-8 for the
// underlying evidence, and SALEAE_DECODER_NOTES.md's 2026-08-31 entries for
// this exact algorithm (post the real-hardware pairing-bug fix) being the
// most-validated embodiment of these rules in the project.
//
// Deliberately knows nothing about what any address/data value *means* -
// that's fn_pcb085_profile.h's job, per CLAUDE.md's layering rule ("keep
// board interpretation separate from common FN decoding").

// SHORT/LONG split and sync-boundary thresholds, in microseconds - see
// FnSymbolAnalyzerSettings.h's mFnShortLongSplitUs/mFnSyncMinUs (same
// values, same evidence).
constexpr uint16_t kFnDecodeNoiseThresholdUs = 3;    // sub-noise-floor glitch/ringing - ignored outright, not counted as an interval
constexpr uint16_t kFnDecodeShortLongSplitUs = 100;  // observed SHORT ~25-27us, LONG ~180-182us
constexpr uint16_t kFnDecodeSyncMinUs = 1000;        // observed sync gaps ~1.2645-1.2647ms or ~1.4195-1.4198ms

struct FnDecodedWord
{
    bool addressBits[5]; // A1..A5, left-to-right per this project's "10001"-style notation
    bool dataBits[4];    // D1..D4
    bool isSecondCopy;   // true if this word's address matched the immediately preceding word's (see class comment)
    bool copiesMatch;    // only meaningful if isSecondCopy - false is a real protocol-level mismatch worth surfacing
};

// One decoder instance tracks one continuous edge stream. Pairing is keyed
// by ADDRESS, not alternating parity - FN-MAIN cycles through several
// different addresses before returning to any one of them, so two
// temporally-adjacent words are not necessarily a copy pair just because
// they're adjacent (this is the exact bug FnSymbolAnalyzer.cpp's
// FinishFnWord() comment describes hitting on real hardware).
class FnWordDecoder
{
public:
    // Feeds one edge-to-edge interval (its duration in microseconds, as
    // measured by whatever's producing the edge stream - real RMT RX
    // eventually, a simulated capture replay today). Returns true and
    // fills *outWord if this interval completed a decodable 9-bit word,
    // either by reaching the 9th bit or by a sync boundary finalizing an
    // in-progress word. A malformed field (interval pattern doesn't match
    // SLSL/LSLS, truncated or not) or an incomplete word (sync arrived
    // before 9 bits) is silently dropped - not surfaced as an outWord -
    // matching FinishFnField()/FinishFnWord()'s error-frame cases, which
    // this pod has no bubble/tabular display to render anyway. errorCount
    // increments on either case, for diagnostics.
    bool feed(uint16_t durationUs, FnDecodedWord *outWord);

    uint32_t errorCount() const { return m_errorCount; }

    // Drops all in-progress field/word/pairing state, e.g. when starting a
    // fresh simulation playback - without this, the first few edges after
    // a restart could spuriously pair against whatever word was last
    // in-progress before the reset.
    void reset();

private:
    bool m_fieldIsLong[4] = {};
    uint32_t m_fieldCount = 0;

    bool m_wordBits[9] = {};
    uint32_t m_wordBitCount = 0;
    bool m_wordHasSymbolWarning = false;

    bool m_havePreviousWord = false;
    bool m_previousWordBits[9] = {};

    uint32_t m_errorCount = 0;

    void finishField();
    bool finishWord(FnDecodedWord *outWord);
};
