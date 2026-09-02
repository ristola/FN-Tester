#include "fn_pcb085_profile.h"

namespace
{
    // Address bit strings transcribed directly from
    // docs/PCB085_ANALYSIS.md's "Known FN Addresses" table - [0]=A1..[4]=A5.
    constexpr bool kAddr10001[5] = {true, false, false, false, true};
    constexpr bool kAddr10010[5] = {true, false, false, true, false};
    constexpr bool kAddr10110[5] = {true, false, true, true, false};
    constexpr bool kAddr10100[5] = {true, false, true, false, false};

    uint8_t analog_code_from_percent(uint8_t percent)
    {
        if (percent > 100)
            percent = 100;
        // docs/PCB085_ANALYSIS.md section 12: code = floor(percent * 15 / 100)
        return static_cast<uint8_t>((static_cast<uint16_t>(percent) * 15) / 100);
    }
}

size_t fn_pcb085_profile_build_cycle(const bool outputs[kFnPcb085MaxOutputs], uint8_t analogPercent,
                                      rmt_data_t *out, size_t outCapacity)
{
    if (outCapacity < kFnPcb085MaxWords)
        return 0;

    bool level = false;
    size_t total = 0;

    // 10001 - Outputs 1-4 (D1=Output1/Alarm .. D4=Output4/Process Blower).
    {
        bool data[4] = {outputs[0], outputs[1], outputs[2], outputs[3]};
        size_t n = fn_symbol_codec_encode_word(kAddr10001, data, out + total, outCapacity - total, &level);
        if (n == 0)
            return 0;
        total += n;
    }

    // 10010 - Outputs 5-8 (D1=Output5/Regen Blower .. D4=Output8/Process Heater).
    {
        bool data[4] = {outputs[4], outputs[5], outputs[6], outputs[7]};
        size_t n = fn_symbol_codec_encode_word(kAddr10010, data, out + total, outCapacity - total, &level);
        if (n == 0)
            return 0;
        total += n;
    }

    uint8_t code = analog_code_from_percent(analogPercent);
    bool b0 = (code & 0x1) != 0;
    bool b1 = (code & 0x2) != 0;
    bool b2 = (code & 0x4) != 0;
    bool b3 = (code & 0x8) != 0;

    // 10110 - analog code, LSB-first: D1=bit0, D2=bit1, D3=bit2, D4=bit3
    // (docs/PCB085_ANALYSIS.md section 12).
    {
        bool data[4] = {b0, b1, b2, b3};
        size_t n = fn_symbol_codec_encode_word(kAddr10110, data, out + total, outCapacity - total, &level);
        if (n == 0)
            return 0;
        total += n;
    }

    // 10100 - analog companion, EXPERIMENTAL (STRONG EVIDENCE / NOT FULLY
    // CONFIRMED - see this file's header comment). Working formula from
    // docs/PCB085_ANALYSIS.md section 14: D1=b0, D2=b1, D3=b1, D4=b0.
    {
        bool data[4] = {b0, b1, b1, b0};
        size_t n = fn_symbol_codec_encode_word(kAddr10100, data, out + total, outCapacity - total, &level);
        if (n == 0)
            return 0;
        total += n;
    }

    return total;
}
