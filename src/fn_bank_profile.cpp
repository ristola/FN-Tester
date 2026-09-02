#include "fn_bank_profile.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <cstring>

#include "app_state.h"

namespace
{
    FnBank s_banks[kFnMaxBanks];
    int s_bank_count = 0;

    const char *kProfilePaths[2] = {
        "/board_profiles/pcb110.csv",
        "/board_profiles/pcb085.csv",
    };

    void clear_slot(FnBankSlot &slot)
    {
        slot.name[0] = '\0';
        slot.confidence = FN_CONF_UNKNOWN;
    }

    void set_bank(FnBank &bank, const char *address, uint8_t kind)
    {
        strncpy(bank.address, address, sizeof(bank.address) - 1);
        bank.address[sizeof(bank.address) - 1] = '\0';
        bank.kind = kind;
        for (auto &slot : bank.slot)
            clear_slot(slot);
    }

    void name_slot(FnBankSlot &slot, const char *name, uint8_t confidence)
    {
        strncpy(slot.name, name, sizeof(slot.name) - 1);
        slot.name[sizeof(slot.name) - 1] = '\0';
        slot.confidence = confidence;
    }

    // Known-good starting point for PCB-085, transcribed from
    // FN_OUTPUT_Tester_Handoff/docs/PCB085_ANALYSIS.md /
    // FN_OUTPUT_Tester_Handoff/CLAUDE.md's "PCB-085 Known Address Mapping" -
    // only what's actually CONFIRMED/STRONG EVIDENCE there. Addresses 10000
    // and 10011 (candidate Outputs 9-12/13-16) are deliberately NOT seeded -
    // per that doc's evidence rules, this tool shouldn't pre-suggest
    // unverified output assignments; an operator adds those via "Add Bank"
    // once actually captured. PCB-110 has no seed at all - no confirmed
    // address/output correlation exists for it yet, this whole profile is
    // meant to be discovered from scratch through this screen.
    void seed_pcb085()
    {
        s_bank_count = 0;

        set_bank(s_banks[s_bank_count], "10001", FN_BANK_DIGITAL);
        name_slot(s_banks[s_bank_count].slot[0], "Output 1 / Alarm", FN_CONF_CONFIRMED);
        name_slot(s_banks[s_bank_count].slot[1], "Output 2 / Valve 1", FN_CONF_CONFIRMED);
        name_slot(s_banks[s_bank_count].slot[2], "Output 3 / Valve 2", FN_CONF_CONFIRMED);
        name_slot(s_banks[s_bank_count].slot[3], "Output 4 / Process Blower", FN_CONF_CONFIRMED);
        s_bank_count++;

        set_bank(s_banks[s_bank_count], "10010", FN_BANK_DIGITAL);
        name_slot(s_banks[s_bank_count].slot[0], "Output 5 / Regen Blower", FN_CONF_CONFIRMED);
        name_slot(s_banks[s_bank_count].slot[1], "Output 6 / Regen Heater", FN_CONF_CONFIRMED);
        name_slot(s_banks[s_bank_count].slot[2], "Output 7 / Isolation Valve", FN_CONF_CONFIRMED);
        name_slot(s_banks[s_bank_count].slot[3], "Output 8 / Process Heater", FN_CONF_CONFIRMED);
        s_bank_count++;

        set_bank(s_banks[s_bank_count], "10110", FN_BANK_ANALOG);
        name_slot(s_banks[s_bank_count].slot[0], "Analog command, 0-15 code LSB-first D1-D4", FN_CONF_CONFIRMED);
        s_bank_count++;

        set_bank(s_banks[s_bank_count], "10100", FN_BANK_ANALOG);
        name_slot(s_banks[s_bank_count].slot[0], "Analog companion (relationship unconfirmed)", FN_CONF_STRONG);
        s_bank_count++;
    }

    void seed_empty()
    {
        s_bank_count = 0;
    }

    // Strips characters that would break the '|'-delimited CSV format out
    // of free-text fields before they're ever stored - simpler than a
    // quoting/escaping scheme for a bench tool's operator-entered labels.
    void sanitize_field(char *text)
    {
        for (char *p = text; *p; p++)
            if (*p == '|' || *p == '\n' || *p == '\r')
                *p = ' ';
    }

    bool load_from_fs(const char *path)
    {
        if (!g_fs_ready || !LittleFS.exists(path))
            return false;

        File f = LittleFS.open(path, FILE_READ);
        if (!f)
            return false;

        s_bank_count = 0;
        while (f.available() && s_bank_count < kFnMaxBanks)
        {
            String line = f.readStringUntil('\n');
            line.trim();
            if (line.length() == 0 || line.startsWith("#"))
                continue;

            // address|kind|s0name|s0conf|s1name|s1conf|s2name|s2conf|s3name|s3conf
            constexpr int kFieldCount = 10;
            String fields[kFieldCount];
            int fieldIndex = 0;
            int start = 0;
            for (int i = 0; i <= line.length() && fieldIndex < kFieldCount; i++)
            {
                if (i == line.length() || line[i] == '|')
                {
                    fields[fieldIndex++] = line.substring(start, i);
                    start = i + 1;
                }
            }
            if (fieldIndex < kFieldCount)
                continue; // malformed line - skip rather than guess

            FnBank &bank = s_banks[s_bank_count];
            set_bank(bank, fields[0].c_str(), static_cast<uint8_t>(fields[1].toInt()));
            for (int s = 0; s < 4; s++)
                name_slot(bank.slot[s], fields[2 + s * 2].c_str(), static_cast<uint8_t>(fields[3 + s * 2].toInt()));
            s_bank_count++;
        }
        f.close();
        return true;
    }
}

const char *fn_confidence_label(uint8_t confidence)
{
    switch (confidence)
    {
    case FN_CONF_CONFIRMED:
        return "CONFIRMED";
    case FN_CONF_STRONG:
        return "STRONG EVIDENCE";
    case FN_CONF_HYPOTHESIS:
        return "HYPOTHESIS";
    default:
        return "UNKNOWN";
    }
}

int fn_bank_profile_count()
{
    return s_bank_count;
}

const FnBank *fn_bank_profile_get(int index)
{
    if (index < 0 || index >= s_bank_count)
        return nullptr;
    return &s_banks[index];
}

FnBank *fn_bank_profile_get_mutable(int index)
{
    if (index < 0 || index >= s_bank_count)
        return nullptr;
    return &s_banks[index];
}

int fn_bank_profile_add(const char *address)
{
    if (s_bank_count >= kFnMaxBanks)
        return -1;
    if (strlen(address) != 5)
        return -1;
    for (int i = 0; i < 5; i++)
        if (address[i] != '0' && address[i] != '1')
            return -1;
    for (int i = 0; i < s_bank_count; i++)
        if (strcmp(s_banks[i].address, address) == 0)
            return -1;

    set_bank(s_banks[s_bank_count], address, FN_BANK_UNKNOWN);
    return s_bank_count++;
}

void fn_bank_profile_remove(int index)
{
    if (index < 0 || index >= s_bank_count)
        return;
    for (int i = index + 1; i < s_bank_count; i++)
        s_banks[i - 1] = s_banks[i];
    s_bank_count--;
}

void fn_bank_profile_load(uint8_t model)
{
    uint8_t clamped = fn_output_model_clamped(model);
    if (load_from_fs(kProfilePaths[clamped]))
        return;

    if (clamped == FN_MODEL_PCB085_16)
        seed_pcb085();
    else
        seed_empty();
}

bool fn_bank_profile_save(uint8_t model)
{
    if (!g_fs_ready)
        return false;

    if (!LittleFS.exists("/board_profiles"))
        LittleFS.mkdir("/board_profiles");

    uint8_t clamped = fn_output_model_clamped(model);
    File f = LittleFS.open(kProfilePaths[clamped], FILE_WRITE); // overwrite - this is a curated profile, not raw evidence
    if (!f)
        return false;

    f.println("# FN bank profile - address|kind(0=digital,1=analog,2=unknown)|"
              "d1name|d1conf|d2name|d2conf|d3name|d3conf|d4name|d4conf "
              "(conf: 0=unknown,1=hypothesis,2=strong,3=confirmed)");
    for (int i = 0; i < s_bank_count; i++)
    {
        FnBank &bank = s_banks[i];
        for (auto &slot : bank.slot)
            sanitize_field(slot.name);

        f.printf("%s|%u|%s|%u|%s|%u|%s|%u|%s|%u\n",
                  bank.address, bank.kind,
                  bank.slot[0].name, bank.slot[0].confidence,
                  bank.slot[1].name, bank.slot[1].confidence,
                  bank.slot[2].name, bank.slot[2].confidence,
                  bank.slot[3].name, bank.slot[3].confidence);
    }
    f.close();
    return true;
}
