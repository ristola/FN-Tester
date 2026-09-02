#pragma once

#include <cstdint>

#include "fn_output_model.h"

// Discovered FN address-bank profile: the actual reusable knowledge this
// tester is for building (per FN_OUTPUT_Tester_Handoff/CLAUDE.md's "Board
// Profiles" section - "address-to-output mapping... confidence metadata").
// Kept separate from ui_capture_learn.cpp (which is the *workflow* for
// discovering/editing this data) per that doc's "keep protocol handling
// separate from UI code" rule - this module owns the data model and its SD
// persistence, nothing about touchscreens.
//
// FN's confirmed frame shape (docs/PCB085_ANALYSIS.md) is a 5-bit address
// selecting a "bank," plus a 4-bit D1-D4 data field for that bank. Most
// known banks are DIGITAL: each of the 4 bits is one independently-named
// output (e.g. address 10001 -> D1=Output1/Alarm, D2=Output2/Valve1, ...).
// A few are ANALOG: the whole 4-bit field is one combined value (e.g.
// 10110's 0-15 analog code) rather than 4 independent bits - only slot 0 is
// used for those, holding the value's name/description. UNKNOWN banks exist
// so an operator can record "I saw this address" before figuring out what
// it means.
enum FnBankKind : uint8_t
{
    FN_BANK_DIGITAL = 0, // 4 independently-named outputs, one per D1-D4 bit
    FN_BANK_ANALOG = 1,  // whole D1-D4 field is one combined value (slot 0 only)
    FN_BANK_UNKNOWN = 2, // address seen, meaning not yet determined
};

// Same four-level evidence discipline as the rest of this project's docs -
// see FN_OUTPUT_Tester_Handoff/CLAUDE.md's "Evidence Rules". Never silently
// promote a slot past what it's actually earned.
enum FnConfidence : uint8_t
{
    FN_CONF_UNKNOWN = 0,
    FN_CONF_HYPOTHESIS = 1,
    FN_CONF_STRONG = 2,
    FN_CONF_CONFIRMED = 3,
};

struct FnBankSlot
{
    char name[24]; // e.g. "Regen Heater" - empty if not yet named
    uint8_t confidence; // FnConfidence
};

struct FnBank
{
    char address[6]; // 5 chars of '0'/'1', NUL-terminated (e.g. "10010")
    uint8_t kind;     // FnBankKind
    FnBankSlot slot[4]; // D1..D4; FN_BANK_ANALOG only uses slot[0]
};

constexpr int kFnMaxBanks = 16;

const char *fn_confidence_label(uint8_t confidence);

// ---- In-RAM working profile for whichever model was last loaded ----
//
// Single active profile at a time (matches this screen's single-model-at-a-
// -time UI) - call fn_bank_profile_load() again after switching models.
// Edits (fn_bank_profile_get_mutable()) are immediate in RAM; nothing
// touches the SD card until fn_bank_profile_save().

int fn_bank_profile_count();
const FnBank *fn_bank_profile_get(int index);
FnBank *fn_bank_profile_get_mutable(int index);

// Adds a new bank at `address` (must be exactly 5 chars of '0'/'1', and not
// already present). Starts as FN_BANK_UNKNOWN with empty/FN_CONF_UNKNOWN
// slots. Returns the new index, or -1 if the address is invalid, a
// duplicate, or the table is full (kFnMaxBanks).
int fn_bank_profile_add(const char *address);

void fn_bank_profile_remove(int index);

// Loads the working profile for `model` (FnOutputModel) - from
// /board_profiles/pcb110.csv or pcb085.csv on the SD card if that file
// exists, otherwise from a small compiled-in seed (PCB-085 only, the
// address/output correlations already CONFIRMED in docs/PCB085_ANALYSIS.md
// - PCB-110 seeds empty, since no address/output correlation exists yet for
// it). Discards any unsaved in-RAM edits for whatever was previously
// loaded.
void fn_bank_profile_load(uint8_t model);

// Persists the current in-RAM profile for `model` to SD, overwriting that
// model's file (this is an edited/curated profile, not raw capture
// evidence, so overwriting on save - unlike ui_capture_learn.cpp's
// never-overwrite session logs - is the intended behavior). Returns false
// if the SD card isn't ready.
bool fn_bank_profile_save(uint8_t model);
