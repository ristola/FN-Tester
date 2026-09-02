#include "fn_word_decoder.h"

#include <cstring>

void FnWordDecoder::reset()
{
    m_fieldCount = 0;
    m_wordBitCount = 0;
    m_wordHasSymbolWarning = false;
    m_havePreviousWord = false;
}

// Ported from FnSymbolAnalyzer.cpp's FinishFnField() - see that function's
// comment for the SLSL/LSLS pattern-check rationale. A malformed field
// still contributes its bit (interval 0 alone distinguishes SLSL from
// LSLS) - it just also counts as an error, matching the original emitting
// both a kFnFrameError AND the field's decoded bit.
void FnWordDecoder::finishField()
{
    if (m_fieldCount == 0)
        return; // nothing pending - e.g. two sync intervals arrived back to back

    bool bitValue = m_fieldIsLong[0];

    bool patternOk = (m_fieldCount == 4 || m_fieldCount == 3);
    for (uint32_t i = 1; i < m_fieldCount && patternOk; i++)
    {
        bool expectedLong = bitValue ? (i % 2 == 0) : (i % 2 != 0);
        if (m_fieldIsLong[i] != expectedLong)
            patternOk = false;
    }
    if (!patternOk)
    {
        m_wordHasSymbolWarning = true;
        m_errorCount++;
    }

    if (m_wordBitCount < 9)
        m_wordBits[m_wordBitCount] = bitValue;
    m_wordBitCount++;

    m_fieldCount = 0;
}

// Ported from FnSymbolAnalyzer.cpp's FinishFnWord() - see that function's
// comment for why pairing is keyed by address, not alternating parity.
bool FnWordDecoder::finishWord(FnDecodedWord *outWord)
{
    if (m_wordBitCount == 0)
        return false;

    if (m_wordBitCount != 9)
    {
        // Incomplete word - a sync boundary arrived before 9 bits were
        // collected. Don't pair a broken word against the next one.
        m_havePreviousWord = false;
        m_wordBitCount = 0;
        m_wordHasSymbolWarning = false;
        m_errorCount++;
        return false;
    }

    bool isRepeatOfPrevious = m_havePreviousWord &&
                               memcmp(m_wordBits, m_previousWordBits, 5 * sizeof(bool)) == 0; // same 5-bit address

    outWord->addressBits[0] = m_wordBits[0];
    outWord->addressBits[1] = m_wordBits[1];
    outWord->addressBits[2] = m_wordBits[2];
    outWord->addressBits[3] = m_wordBits[3];
    outWord->addressBits[4] = m_wordBits[4];
    outWord->dataBits[0] = m_wordBits[5];
    outWord->dataBits[1] = m_wordBits[6];
    outWord->dataBits[2] = m_wordBits[7];
    outWord->dataBits[3] = m_wordBits[8];
    outWord->isSecondCopy = isRepeatOfPrevious;
    outWord->copiesMatch = isRepeatOfPrevious && memcmp(m_wordBits, m_previousWordBits, 9 * sizeof(bool)) == 0;

    if (isRepeatOfPrevious)
    {
        m_havePreviousWord = false; // pair consumed - the next word starts fresh
    }
    else
    {
        memcpy(m_previousWordBits, m_wordBits, sizeof(m_wordBits));
        m_havePreviousWord = true;
    }

    m_wordBitCount = 0;
    m_wordHasSymbolWarning = false;
    return true;
}

bool FnWordDecoder::feed(uint16_t durationUs, FnDecodedWord *outWord)
{
    if (durationUs < kFnDecodeNoiseThresholdUs)
        return false; // sub-noise-floor glitch/ringing - not a real S/L interval, ignored outright

    if (durationUs >= kFnDecodeSyncMinUs)
    {
        // Word/frame boundary: finalize whatever field/word was in
        // progress. Unlike the normal accumulation path below, this always
        // finalizes the word (complete or not) - a sync boundary ends
        // whatever was in flight, per FN_PROTOCOL_FINDINGS.md section 4.
        finishField();
        return finishWord(outWord);
    }

    bool isLong = durationUs >= kFnDecodeShortLongSplitUs;

    if (m_fieldCount < 4)
        m_fieldIsLong[m_fieldCount] = isLong;
    m_fieldCount++;

    if (m_fieldCount >= 4)
        finishField();

    if (m_wordBitCount == 9)
        return finishWord(outWord);

    return false;
}
