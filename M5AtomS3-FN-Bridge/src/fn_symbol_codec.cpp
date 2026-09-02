#include "fn_symbol_codec.h"

namespace
{
    // Accumulates a stream of (duration, level) intervals into rmt_data_t
    // pairs (2 intervals per struct). Every caller in this file emits an
    // even total interval count per fn_symbol_codec_encode_word() call
    // (see that function's header comment), so `pending` is always
    // resolved by the time a call finishes - flush()-on-destruct isn't
    // needed here.
    struct IntervalWriter
    {
        rmt_data_t *out;
        size_t capacity;
        size_t wordCount = 0;
        bool havePending = false;
        uint16_t pendingDuration = 0;
        bool pendingLevel = false;
        bool overflowed = false;

        IntervalWriter(rmt_data_t *outBuf, size_t outCapacity) : out(outBuf), capacity(outCapacity) {}

        void push(uint16_t durationUs, bool level)
        {
            if (!havePending)
            {
                pendingDuration = durationUs;
                pendingLevel = level;
                havePending = true;
                return;
            }
            if (wordCount >= capacity)
            {
                overflowed = true;
                havePending = false;
                return;
            }
            out[wordCount].duration0 = pendingDuration;
            out[wordCount].level0 = pendingLevel ? 1 : 0;
            out[wordCount].duration1 = durationUs;
            out[wordCount].level1 = level ? 1 : 0;
            wordCount++;
            havePending = false;
        }
    };

    void emit_interval(IntervalWriter &w, uint16_t durationUs, bool &level)
    {
        level = !level;
        w.push(durationUs, level);
    }

    // 0 -> Short Long Short Long, 1 -> Long Short Long Short
    // (FN_PROTOCOL_FINDINGS.md section 7).
    void emit_bit(IntervalWriter &w, bool bitValue, bool &level)
    {
        uint16_t first = bitValue ? kFnLongUs : kFnShortUs;
        uint16_t second = bitValue ? kFnShortUs : kFnLongUs;
        emit_interval(w, first, level);
        emit_interval(w, second, level);
        emit_interval(w, first, level);
        emit_interval(w, second, level);
    }

    void emit_copy(IntervalWriter &w, const bool addressBits[5], const bool dataBits[4], bool &level)
    {
        for (int i = 0; i < 5; i++)
            emit_bit(w, addressBits[i], level);
        for (int i = 0; i < 4; i++)
            emit_bit(w, dataBits[i], level);
    }
}

size_t fn_symbol_codec_encode_word(const bool addressBits[5], const bool dataBits[4],
                                    rmt_data_t *out, size_t outCapacity, bool *level)
{
    IntervalWriter w{out, outCapacity};

    emit_copy(w, addressBits, dataBits, *level);
    emit_interval(w, kFnSyncUs, *level);
    emit_copy(w, addressBits, dataBits, *level);
    emit_interval(w, kFnSyncUs, *level); // gap before the next address block in the cycle

    if (w.overflowed || w.havePending)
        return 0;
    return w.wordCount;
}
