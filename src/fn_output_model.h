#pragma once

#include <cstdint>

// Shared OUTPUT-board profile identifiers, used by both the FN Output
// (ui_fn_output.cpp) and Capture/Learn (ui_capture_learn.cpp) screens, and
// by AppConfig::fn_output_model (config.h) - one setting so the operator
// picks the physical board once and every screen agrees on it. Matches
// FN_OUTPUT_Tester_Handoff/docs/PCB110_ANALYSIS.md and
// docs/PCB085_ANALYSIS.md.
enum FnOutputModel : uint8_t
{
    FN_MODEL_PCB110_10 = 0, // 10 outputs, no analog
    FN_MODEL_PCB085_16 = 1, // 16 outputs + one 4-20mA analog channel
};

struct FnModelInfo
{
    uint8_t outputCount;
    bool hasAnalog;
    const char *label;
};

constexpr FnModelInfo kFnModels[2] = {
    {10, false, "PCB-110 (10 OUT)"},
    {16, true, "PCB-085 (16 OUT)"},
};
constexpr int kFnMaxOutputs = 16;

inline uint8_t fn_output_model_clamped(uint8_t model)
{
    return model <= FN_MODEL_PCB085_16 ? model : FN_MODEL_PCB085_16;
}
