#include "ui_capture_learn.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <cstdarg>

#include "app_state.h"
#include "fn_bank_profile.h"
#include "fn_output_model.h"

namespace
{
    // ---- Session log ----
    //
    // Ring buffer of manually-marked events (see ui_capture_learn.h) - same
    // shape as espnow_state.cpp's traffic log (fixed pool, write cursor,
    // oldest overwritten once full), just page-local since nothing else
    // needs this data.
    struct CaptureLogEntry
    {
        uint32_t sessionMs; // millis() elapsed since this screen's log was last cleared
        char text[40];
    };
    constexpr int kMaxCaptureLog = 60;
    CaptureLogEntry s_log[kMaxCaptureLog];
    int s_log_count = 0;
    int s_log_next = 0;
    uint32_t s_session_start_ms = 0;

    void log_clear()
    {
        s_log_count = 0;
        s_log_next = 0;
        s_session_start_ms = millis();
    }

    void log_add(const char *fmt, ...)
    {
        CaptureLogEntry &e = s_log[s_log_next];
        e.sessionMs = millis() - s_session_start_ms;
        va_list args;
        va_start(args, fmt);
        vsnprintf(e.text, sizeof(e.text), fmt, args);
        va_end(args);

        s_log_next = (s_log_next + 1) % kMaxCaptureLog;
        if (s_log_count < kMaxCaptureLog)
            s_log_count++;
    }

    // index 0 = most recently logged.
    const CaptureLogEntry *log_entry(int indexFromNewest)
    {
        if (indexFromNewest < 0 || indexFromNewest >= s_log_count)
            return nullptr;
        int idx = (s_log_next - 1 - indexFromNewest + kMaxCaptureLog * 2) % kMaxCaptureLog;
        return &s_log[idx];
    }

    // ---- Main page ----

    lv_obj_t *s_overlay = nullptr;
    lv_obj_t *s_model_btn[2] = {nullptr, nullptr};
    lv_obj_t *s_reference_lbl = nullptr;
    uint8_t s_loaded_model = 0xFF; // sentinel - forces fn_bank_profile_load() on the first apply_model_to_ui()

    // ---- Mark Outputs sub-page ----

    lv_obj_t *s_mark_overlay = nullptr;
    lv_obj_t *s_mark_output_btn[kFnMaxOutputs] = {nullptr};
    bool s_mark_output_state[kFnMaxOutputs] = {false};
    lv_obj_t *s_mark_analog_row = nullptr;
    lv_obj_t *s_mark_analog_slider = nullptr;
    lv_obj_t *s_mark_analog_value_lbl = nullptr;
    lv_obj_t *s_mark_status_lbl = nullptr;
    bool s_mark_visible = false;

    // ---- Session Log sub-page ----

    lv_obj_t *s_log_overlay = nullptr;
    lv_obj_t *s_log_filename_ta = nullptr;
    lv_obj_t *s_log_save_status_lbl = nullptr;
    lv_obj_t *s_log_wrap = nullptr;
    lv_obj_t *s_log_empty_lbl = nullptr;
    lv_obj_t *s_log_rows[kMaxCaptureLog];
    bool s_log_visible = false;

    // ---- Address Banks sub-page (list) ----

    lv_obj_t *s_banks_overlay = nullptr;
    lv_obj_t *s_bank_rows[kFnMaxBanks];
    lv_obj_t *s_banks_empty_lbl = nullptr;
    lv_obj_t *s_banks_status_lbl = nullptr;

    // ---- Edit Bank sub-page ----

    lv_obj_t *s_bank_edit_overlay = nullptr;
    lv_obj_t *s_bank_edit_title_lbl = nullptr;
    lv_obj_t *s_kind_btn[3] = {nullptr}; // Digital / Analog / Unknown
    lv_obj_t *s_slot_row[4] = {nullptr};
    lv_obj_t *s_slot_d_lbl[4] = {nullptr};
    lv_obj_t *s_slot_name_lbl[4] = {nullptr};
    lv_obj_t *s_slot_conf_btn[4] = {nullptr};
    lv_obj_t *s_unknown_note_lbl = nullptr;
    int s_edit_bank_index = -1;

    // ---- Shared text-entry keyboard overlay ----
    //
    // One dedicated full-page keyboard screen reused for every free-text
    // field on this page (Saleae filename, new bank address, each slot
    // name) instead of a separate overlay per field - the pattern this
    // project already used once (the ESP NOW page's Name field); with six
    // text fields here it's worth sharing.
    enum class TextEntryTarget
    {
        kFilename,
        kNewBankAddress,
        kSlotName,
    };
    lv_obj_t *s_text_entry_overlay = nullptr;
    lv_obj_t *s_text_entry_ta = nullptr;
    lv_obj_t *s_text_entry_keyboard = nullptr;
    lv_obj_t *s_text_entry_title_lbl = nullptr;
    TextEntryTarget s_text_entry_target = TextEntryTarget::kFilename;
    int s_text_entry_slot_index = -1; // valid only for kSlotName

    uint8_t current_model()
    {
        return fn_output_model_clamped(g_config.fn_output_model);
    }

    // Known address-to-output map, per FN_OUTPUT_Tester_Handoff/CLAUDE.md's
    // "PCB-085 Known Address Mapping" section (CONFIRMED/STRONG EVIDENCE as
    // marked there) - shown as a reference while the technician operates the
    // real board. PCB-110 has no confirmed map yet (only circuit-level
    // findings in docs/PCB110_ANALYSIS.md, no output correlation) - that's
    // exactly the gap the Address Banks page below is for closing.
    const char *reference_text_for_model(uint8_t model)
    {
        if (model == FN_MODEL_PCB085_16)
        {
            return "Known PCB-085 address map (see docs/PCB085_ANALYSIS.md):\n"
                   "10001 -> Outputs 1-4: Alarm, Valve 1, Valve 2, Process Blower (CONFIRMED)\n"
                   "10010 -> Outputs 5-8: Regen Blower, Regen Heater, Isolation Valve, "
                   "Process Heater (CONFIRMED)\n"
                   "10110 -> Analog code 0-15, LSB-first D1-D4 (CONFIRMED/STRONG EVIDENCE)\n"
                   "10100 -> Analog companion value (STRONG EVIDENCE, not fully confirmed)\n"
                   "10000, 10011 -> UNKNOWN (candidates: Outputs 9-12 / 13-16 - not yet "
                   "assigned, per project rules)\n\n"
                   "See Address Banks for the editable, saved version of this table.";
        }
        return "No confirmed address map for PCB-110 yet - only circuit-level findings "
               "exist so far (docs/PCB110_ANALYSIS.md). Toggle individual outputs on the "
               "real board one at a time, mark them below, and record the address you "
               "observe on Address Banks to build one from scratch.";
    }

    void refresh_banks_list()
    {
        int count = fn_bank_profile_count();
        if (count == 0)
            lv_obj_clear_flag(s_banks_empty_lbl, LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_add_flag(s_banks_empty_lbl, LV_OBJ_FLAG_HIDDEN);

        for (int i = 0; i < kFnMaxBanks; i++)
        {
            if (i >= count)
            {
                lv_obj_add_flag(s_bank_rows[i], LV_OBJ_FLAG_HIDDEN);
                continue;
            }

            const FnBank *bank = fn_bank_profile_get(i);
            char summary[160]; // comfortably fits address + kind tag + 4 slot names at their max length
            if (bank->kind == FN_BANK_DIGITAL)
            {
                snprintf(summary, sizeof(summary), "%s [DIGITAL] %s | %s | %s | %s", bank->address,
                         bank->slot[0].name[0] ? bank->slot[0].name : "-",
                         bank->slot[1].name[0] ? bank->slot[1].name : "-",
                         bank->slot[2].name[0] ? bank->slot[2].name : "-",
                         bank->slot[3].name[0] ? bank->slot[3].name : "-");
            }
            else if (bank->kind == FN_BANK_ANALOG)
            {
                snprintf(summary, sizeof(summary), "%s [ANALOG] %s", bank->address,
                         bank->slot[0].name[0] ? bank->slot[0].name : "-");
            }
            else
            {
                snprintf(summary, sizeof(summary), "%s [UNKNOWN] - tap to characterize", bank->address);
            }
            lv_label_set_text(lv_obj_get_child(s_bank_rows[i], 0), summary);
            lv_obj_clear_flag(s_bank_rows[i], LV_OBJ_FLAG_HIDDEN);
        }
    }

    void apply_model_to_ui()
    {
        uint8_t model = current_model();

        if (model != s_loaded_model)
        {
            fn_bank_profile_load(model);
            s_loaded_model = model;
        }
        refresh_banks_list();

        const FnModelInfo &info = kFnModels[model];
        lv_label_set_text(s_reference_lbl, reference_text_for_model(model));

        for (int i = 0; i < kFnMaxOutputs; i++)
        {
            if (i < info.outputCount)
                lv_obj_clear_flag(s_mark_output_btn[i], LV_OBJ_FLAG_HIDDEN);
            else
                lv_obj_add_flag(s_mark_output_btn[i], LV_OBJ_FLAG_HIDDEN);
        }

        if (info.hasAnalog)
            lv_obj_clear_flag(s_mark_analog_row, LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_add_flag(s_mark_analog_row, LV_OBJ_FLAG_HIDDEN);

        for (int i = 0; i < 2; i++)
        {
            bool active = (i == model);
            lv_obj_set_style_bg_color(s_model_btn[i],
                                       active ? lv_palette_main(LV_PALETTE_BLUE) : lv_palette_darken(LV_PALETTE_GREY, 2),
                                       0);
        }
    }

    void refresh_log_list()
    {
        if (s_log_count == 0)
            lv_obj_clear_flag(s_log_empty_lbl, LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_add_flag(s_log_empty_lbl, LV_OBJ_FLAG_HIDDEN);

        for (int i = 0; i < kMaxCaptureLog; i++)
        {
            if (i >= s_log_count)
            {
                lv_obj_add_flag(s_log_rows[i], LV_OBJ_FLAG_HIDDEN);
                continue;
            }
            const CaptureLogEntry *e = log_entry(i);
            uint32_t sec = e->sessionMs / 1000;
            lv_label_set_text_fmt(s_log_rows[i], "%u:%02u  %s", sec / 60, sec % 60, e->text);
            lv_obj_clear_flag(s_log_rows[i], LV_OBJ_FLAG_HIDDEN);
        }
    }

    // ---- Main page handlers ----

    void on_back_clicked(lv_event_t *)
    {
        lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
    }

    void on_model_btn_clicked(lv_event_t *e)
    {
        uint8_t model = static_cast<uint8_t>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
        if (model == current_model())
            return;

        g_config.fn_output_model = model;
        config_save(g_config);

        for (int i = 0; i < kFnMaxOutputs; i++)
        {
            s_mark_output_state[i] = false;
            lv_obj_clear_state(s_mark_output_btn[i], LV_STATE_CHECKED);
        }
        lv_slider_set_value(s_mark_analog_slider, 0, LV_ANIM_OFF);
        lv_label_set_text(s_mark_analog_value_lbl, "0%  (4.0 mA)");

        log_add("--- switched to %s ---", kFnModels[model].label);
        apply_model_to_ui();
    }

    void on_mark_nav_clicked(lv_event_t *)
    {
        s_mark_visible = true;
        lv_label_set_text(s_mark_status_lbl, "");
        lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_mark_overlay, LV_OBJ_FLAG_HIDDEN);
    }

    void on_log_nav_clicked(lv_event_t *)
    {
        s_log_visible = true;
        lv_label_set_text(s_log_save_status_lbl, "");
        refresh_log_list();
        lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_log_overlay, LV_OBJ_FLAG_HIDDEN);
    }

    void on_banks_nav_clicked(lv_event_t *)
    {
        lv_label_set_text(s_banks_status_lbl, "");
        refresh_banks_list();
        lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_banks_overlay, LV_OBJ_FLAG_HIDDEN);
    }

    void on_banks_back_clicked(lv_event_t *)
    {
        lv_obj_add_flag(s_banks_overlay, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
    }

    // ---- Mark Outputs handlers ----

    void on_mark_back_clicked(lv_event_t *)
    {
        s_mark_visible = false;
        lv_obj_add_flag(s_mark_overlay, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
    }

    void on_mark_output_clicked(lv_event_t *e)
    {
        int index = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
        lv_obj_t *btn = static_cast<lv_obj_t *>(lv_event_get_target(e));
        bool on = lv_obj_has_state(btn, LV_STATE_CHECKED); // LV_OBJ_FLAG_CHECKABLE already toggled this before the event fires
        s_mark_output_state[index] = on;

        log_add("Output %d -> %s", index + 1, on ? "ON" : "OFF");
        lv_label_set_text_fmt(s_mark_status_lbl, "Logged: Output %d -> %s", index + 1, on ? "ON" : "OFF");
    }

    void on_mark_all_off_clicked(lv_event_t *)
    {
        uint8_t count = kFnModels[current_model()].outputCount;
        for (int i = 0; i < count; i++)
        {
            s_mark_output_state[i] = false;
            lv_obj_clear_state(s_mark_output_btn[i], LV_STATE_CHECKED);
        }
        log_add("--- all outputs marked off ---");
        lv_label_set_text(s_mark_status_lbl, "Logged: all outputs off");
    }

    void on_mark_analog_value_changed(lv_event_t *e)
    {
        lv_obj_t *slider = static_cast<lv_obj_t *>(lv_event_get_target(e));
        int32_t percent = lv_slider_get_value(slider);
        float milliamps = 4.0f + (percent / 100.0f) * 16.0f;
        lv_label_set_text_fmt(s_mark_analog_value_lbl, "%d%%  (%.1f mA)", static_cast<int>(percent), static_cast<double>(milliamps));
    }

    void on_mark_analog_log_clicked(lv_event_t *)
    {
        int32_t percent = lv_slider_get_value(s_mark_analog_slider);
        log_add("Analog -> %d%%", static_cast<int>(percent));
        lv_label_set_text_fmt(s_mark_status_lbl, "Logged: Analog -> %d%%", static_cast<int>(percent));
    }

    // ---- Session Log handlers ----

    void on_log_back_clicked(lv_event_t *)
    {
        s_log_visible = false;
        lv_obj_add_flag(s_log_overlay, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
    }

    void on_clear_log_clicked(lv_event_t *)
    {
        log_clear();
        refresh_log_list();
        lv_label_set_text(s_log_save_status_lbl, "Log cleared");
        lv_obj_set_style_text_color(s_log_save_status_lbl, lv_palette_main(LV_PALETTE_GREY), 0);
    }

    // Writes the current session log to /capture_learn/session_NNN.csv on
    // the internal filesystem (first unused NNN - never overwrites an earlier session,
    // matching this project's "never overwrite raw evidence" rule). Each
    // row is elapsed session time + the logged event; the header carries
    // the board model and the operator-entered Saleae capture filename so
    // the CSV stays correlated with that external evidence.
    void save_session_to_fs()
    {
        if (!g_fs_ready)
        {
            lv_label_set_text(s_log_save_status_lbl, "Internal filesystem not ready - can't save");
            lv_obj_set_style_text_color(s_log_save_status_lbl, lv_palette_main(LV_PALETTE_ORANGE), 0);
            return;
        }
        if (s_log_count == 0)
        {
            lv_label_set_text(s_log_save_status_lbl, "Nothing logged yet - nothing to save");
            lv_obj_set_style_text_color(s_log_save_status_lbl, lv_palette_main(LV_PALETTE_ORANGE), 0);
            return;
        }

        if (!LittleFS.exists("/capture_learn"))
            LittleFS.mkdir("/capture_learn");

        char path[64];
        int n = 1;
        do
        {
            snprintf(path, sizeof(path), "/capture_learn/session_%03d.csv", n++);
        } while (LittleFS.exists(path) && n < 1000);

        File f = LittleFS.open(path, FILE_WRITE);
        if (!f)
        {
            lv_label_set_text(s_log_save_status_lbl, "Failed to open file on internal filesystem");
            lv_obj_set_style_text_color(s_log_save_status_lbl, lv_palette_main(LV_PALETTE_RED), 0);
            return;
        }

        f.printf("model,%s\n", kFnModels[current_model()].label);
        f.printf("saleae_capture_file,%s\n", lv_textarea_get_text(s_log_filename_ta));
        f.println("elapsed_sec,event");
        for (int i = s_log_count - 1; i >= 0; i--) // oldest first
        {
            const CaptureLogEntry *e = log_entry(i);
            f.printf("%u,%s\n", e->sessionMs / 1000, e->text);
        }
        f.close();

        lv_label_set_text_fmt(s_log_save_status_lbl, "Saved as %s", path);
        lv_obj_set_style_text_color(s_log_save_status_lbl, lv_palette_main(LV_PALETTE_GREEN), 0);
    }

    void on_save_log_clicked(lv_event_t *)
    {
        save_session_to_fs();
    }

    // ---- Address Banks (list) handlers ----

    void open_bank_editor(int index); // fwd - referenced by the shared text-entry's kNewBankAddress case below

    void on_bank_row_clicked(lv_event_t *e)
    {
        int index = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
        open_bank_editor(index);
    }

    void open_text_entry(lv_obj_t *return_overlay, const char *title, const char *initial_text,
                          lv_keyboard_mode_t mode, TextEntryTarget target)
    {
        s_text_entry_target = target;
        lv_label_set_text(s_text_entry_title_lbl, title);
        lv_textarea_set_text(s_text_entry_ta, initial_text);
        lv_keyboard_set_mode(s_text_entry_keyboard, mode);
        lv_obj_add_flag(return_overlay, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_text_entry_overlay, LV_OBJ_FLAG_HIDDEN);
    }

    void on_add_bank_clicked(lv_event_t *)
    {
        open_text_entry(s_banks_overlay, "New Bank Address (5 digits, 0/1)", "", LV_KEYBOARD_MODE_NUMBER,
                         TextEntryTarget::kNewBankAddress);
    }

    void on_save_profile_clicked(lv_event_t *)
    {
        if (fn_bank_profile_save(current_model()))
        {
            lv_label_set_text(s_banks_status_lbl, "Profile saved");
            lv_obj_set_style_text_color(s_banks_status_lbl, lv_palette_main(LV_PALETTE_GREEN), 0);
        }
        else
        {
            lv_label_set_text(s_banks_status_lbl, "Internal filesystem not ready - can't save");
            lv_obj_set_style_text_color(s_banks_status_lbl, lv_palette_main(LV_PALETTE_ORANGE), 0);
        }
    }

    // ---- Edit Bank handlers ----

    void refresh_bank_editor()
    {
        const FnBank *bank = fn_bank_profile_get(s_edit_bank_index);
        if (bank == nullptr)
            return;

        lv_label_set_text_fmt(s_bank_edit_title_lbl, "Bank %s", bank->address);

        for (int i = 0; i < 3; i++)
            lv_obj_set_style_bg_color(s_kind_btn[i],
                                       (i == bank->kind) ? lv_palette_main(LV_PALETTE_BLUE) : lv_palette_darken(LV_PALETTE_GREY, 2),
                                       0);

        int visibleSlots = (bank->kind == FN_BANK_DIGITAL) ? 4 : (bank->kind == FN_BANK_ANALOG) ? 1
                                                                                                  : 0;
        for (int i = 0; i < 4; i++)
        {
            if (i >= visibleSlots)
            {
                lv_obj_add_flag(s_slot_row[i], LV_OBJ_FLAG_HIDDEN);
                continue;
            }
            lv_obj_clear_flag(s_slot_row[i], LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text(s_slot_d_lbl[i], bank->kind == FN_BANK_ANALOG ? "Value:" : (i == 0 ? "D1:" : i == 1 ? "D2:"
                                                                                                        : i == 2   ? "D3:"
                                                                                                                   : "D4:"));
            lv_label_set_text(s_slot_name_lbl[i], bank->slot[i].name[0] ? bank->slot[i].name : "(tap to name)");
            lv_label_set_text(lv_obj_get_child(s_slot_conf_btn[i], 0), fn_confidence_label(bank->slot[i].confidence));

            lv_color_t confColor;
            switch (bank->slot[i].confidence)
            {
            case FN_CONF_CONFIRMED:
                confColor = lv_palette_main(LV_PALETTE_GREEN);
                break;
            case FN_CONF_STRONG:
                confColor = lv_palette_main(LV_PALETTE_TEAL);
                break;
            case FN_CONF_HYPOTHESIS:
                confColor = lv_palette_main(LV_PALETTE_ORANGE);
                break;
            default:
                confColor = lv_palette_darken(LV_PALETTE_GREY, 2);
                break;
            }
            lv_obj_set_style_bg_color(s_slot_conf_btn[i], confColor, 0);
        }

        if (bank->kind == FN_BANK_UNKNOWN)
            lv_obj_clear_flag(s_unknown_note_lbl, LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_add_flag(s_unknown_note_lbl, LV_OBJ_FLAG_HIDDEN);
    }

    void open_bank_editor(int index)
    {
        s_edit_bank_index = index;
        refresh_bank_editor();
        lv_obj_add_flag(s_banks_overlay, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_bank_edit_overlay, LV_OBJ_FLAG_HIDDEN);
    }

    void on_bank_edit_back_clicked(lv_event_t *)
    {
        lv_obj_add_flag(s_bank_edit_overlay, LV_OBJ_FLAG_HIDDEN);
        refresh_banks_list();
        lv_obj_clear_flag(s_banks_overlay, LV_OBJ_FLAG_HIDDEN);
    }

    void on_kind_clicked(lv_event_t *e)
    {
        uint8_t kind = static_cast<uint8_t>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
        FnBank *bank = fn_bank_profile_get_mutable(s_edit_bank_index);
        if (bank == nullptr)
            return;
        bank->kind = kind;
        refresh_bank_editor();
    }

    void on_slot_name_clicked(lv_event_t *e)
    {
        int slot = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
        const FnBank *bank = fn_bank_profile_get(s_edit_bank_index);
        if (bank == nullptr)
            return;
        s_text_entry_slot_index = slot;
        open_text_entry(s_bank_edit_overlay, bank->kind == FN_BANK_ANALOG ? "Value Name" : "Output Name",
                         bank->slot[slot].name, LV_KEYBOARD_MODE_TEXT_UPPER, TextEntryTarget::kSlotName);
    }

    void on_slot_conf_clicked(lv_event_t *e)
    {
        int slot = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
        FnBank *bank = fn_bank_profile_get_mutable(s_edit_bank_index);
        if (bank == nullptr)
            return;
        bank->slot[slot].confidence = static_cast<uint8_t>((bank->slot[slot].confidence + 1) % 4);
        refresh_bank_editor();
    }

    void on_delete_bank_clicked(lv_event_t *)
    {
        fn_bank_profile_remove(s_edit_bank_index);
        s_edit_bank_index = -1;
        lv_obj_add_flag(s_bank_edit_overlay, LV_OBJ_FLAG_HIDDEN);
        refresh_banks_list();
        lv_obj_clear_flag(s_banks_overlay, LV_OBJ_FLAG_HIDDEN);
    }

    // ---- Shared text-entry keyboard overlay handlers ----

    void on_filename_focused(lv_event_t *)
    {
        open_text_entry(s_log_overlay, "Saleae Capture File", lv_textarea_get_text(s_log_filename_ta),
                         LV_KEYBOARD_MODE_TEXT_LOWER, TextEntryTarget::kFilename);
    }

    void on_text_entry_cancel_clicked(lv_event_t *)
    {
        lv_obj_add_flag(s_text_entry_overlay, LV_OBJ_FLAG_HIDDEN);
        switch (s_text_entry_target)
        {
        case TextEntryTarget::kFilename:
            lv_obj_clear_flag(s_log_overlay, LV_OBJ_FLAG_HIDDEN);
            break;
        case TextEntryTarget::kNewBankAddress:
            lv_obj_clear_flag(s_banks_overlay, LV_OBJ_FLAG_HIDDEN);
            break;
        case TextEntryTarget::kSlotName:
            lv_obj_clear_flag(s_bank_edit_overlay, LV_OBJ_FLAG_HIDDEN);
            break;
        }
    }

    // Listens on the textarea, not the keyboard - the return-arrow key only
    // fires LV_EVENT_READY on the bound textarea, not the keyboard object.
    void on_text_entry_ready(lv_event_t *)
    {
        String text = lv_textarea_get_text(s_text_entry_ta);
        lv_obj_add_flag(s_text_entry_overlay, LV_OBJ_FLAG_HIDDEN);

        switch (s_text_entry_target)
        {
        case TextEntryTarget::kFilename:
            lv_textarea_set_text(s_log_filename_ta, text.c_str());
            lv_obj_clear_flag(s_log_overlay, LV_OBJ_FLAG_HIDDEN);
            break;

        case TextEntryTarget::kNewBankAddress:
        {
            text.trim();
            int idx = (text.length() == 5) ? fn_bank_profile_add(text.c_str()) : -1;
            if (idx < 0)
            {
                lv_label_set_text(s_banks_status_lbl, "Invalid or duplicate address - need exactly 5 digits of 0/1");
                lv_obj_set_style_text_color(s_banks_status_lbl, lv_palette_main(LV_PALETTE_ORANGE), 0);
                refresh_banks_list();
                lv_obj_clear_flag(s_banks_overlay, LV_OBJ_FLAG_HIDDEN);
            }
            else
            {
                refresh_banks_list();
                open_bank_editor(idx); // jump straight into naming the new bank's slots
            }
            break;
        }

        case TextEntryTarget::kSlotName:
        {
            FnBank *bank = fn_bank_profile_get_mutable(s_edit_bank_index);
            if (bank != nullptr && s_text_entry_slot_index >= 0 && s_text_entry_slot_index < 4)
            {
                strncpy(bank->slot[s_text_entry_slot_index].name, text.c_str(), sizeof(bank->slot[0].name) - 1);
                bank->slot[s_text_entry_slot_index].name[sizeof(bank->slot[0].name) - 1] = '\0';
            }
            refresh_bank_editor();
            lv_obj_clear_flag(s_bank_edit_overlay, LV_OBJ_FLAG_HIDDEN);
            break;
        }
        }
    }

    // ---- Shared small helpers ----

    lv_obj_t *make_header(lv_obj_t *parent, const char *title, lv_event_cb_t back_cb)
    {
        lv_obj_t *header = lv_obj_create(parent);
        lv_obj_remove_style_all(header);
        lv_obj_set_width(header, LV_PCT(100));
        lv_obj_set_height(header, 56);
        lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *back_btn = lv_button_create(header);
        lv_obj_set_size(back_btn, 100, 56);
        lv_obj_align(back_btn, LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_t *back_lbl = lv_label_create(back_btn);
        lv_label_set_text(back_lbl, "Back");
        lv_obj_set_style_text_font(back_lbl, &lv_font_montserrat_24, 0);
        lv_obj_center(back_lbl);
        lv_obj_add_event_cb(back_btn, back_cb, LV_EVENT_CLICKED, nullptr);

        lv_obj_t *title_lbl = lv_label_create(header);
        lv_label_set_text(title_lbl, title);
        lv_obj_set_style_text_color(title_lbl, lv_color_white(), 0);
        lv_obj_set_style_text_font(title_lbl, &lv_font_montserrat_28, 0);
        lv_obj_align(title_lbl, LV_ALIGN_CENTER, 0, 0);

        return header;
    }

    lv_obj_t *make_full_screen_overlay()
    {
        lv_obj_t *overlay = lv_obj_create(lv_layer_top());
        lv_obj_add_flag(overlay, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_size(overlay, LV_PCT(100), LV_PCT(100));
        lv_obj_set_style_bg_color(overlay, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(overlay, LV_OPA_COVER, 0);
        lv_obj_set_style_pad_all(overlay, 16, 0);
        lv_obj_set_flex_flow(overlay, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(overlay, 8, 0);
        lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
        return overlay;
    }

    lv_obj_t *make_model_btn(lv_obj_t *parent, const char *text, uint8_t model)
    {
        lv_obj_t *btn = lv_button_create(parent);
        lv_obj_set_flex_grow(btn, 1);
        lv_obj_set_height(btn, 52);
        lv_obj_add_event_cb(btn, on_model_btn_clicked, LV_EVENT_CLICKED,
                             reinterpret_cast<void *>(static_cast<intptr_t>(model)));
        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, text);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_24, 0);
        lv_obj_center(lbl);
        return btn;
    }

    // Checkable "mark" tile - visually identical to the FN Output screen's
    // LED grid, but tapping this one only writes a local log entry; nothing
    // is sent anywhere.
    lv_obj_t *make_mark_btn(lv_obj_t *parent, int index)
    {
        lv_obj_t *btn = lv_button_create(parent);
        lv_obj_set_size(btn, 160, 60);
        lv_obj_add_flag(btn, LV_OBJ_FLAG_CHECKABLE);
        lv_obj_set_style_bg_color(btn, lv_palette_darken(LV_PALETTE_GREY, 2), 0);
        lv_obj_set_style_bg_color(btn, lv_palette_main(LV_PALETTE_GREEN), LV_STATE_CHECKED);
        lv_obj_add_event_cb(btn, on_mark_output_clicked, LV_EVENT_CLICKED,
                             reinterpret_cast<void *>(static_cast<intptr_t>(index)));

        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text_fmt(lbl, "Out %d", index + 1);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_24, 0);
        lv_obj_center(lbl);
        return btn;
    }
}

void ui_capture_learn_build_pool()
{
    log_clear();

    // ---- Main page ----

    s_overlay = make_full_screen_overlay();
    make_header(s_overlay, "Capture / Learn", on_back_clicked);

    lv_obj_t *caption = lv_label_create(s_overlay);
    lv_label_set_text(caption,
                       "Manual observation log for OUTPUT board profile development - "
                       "correlate what you did on the real board against your own Saleae "
                       "capture. This tester has no FN-MAIN receive interface yet, so it "
                       "does not decode live FN traffic on-device.");
    lv_obj_set_style_text_color(caption, lv_palette_darken(LV_PALETTE_GREY, 1), 0);
    lv_obj_set_style_text_font(caption, &lv_font_montserrat_18, 0);
    lv_obj_set_width(caption, LV_PCT(100));

    lv_obj_t *model_row = lv_obj_create(s_overlay);
    lv_obj_remove_style_all(model_row);
    lv_obj_set_width(model_row, LV_PCT(100));
    lv_obj_set_height(model_row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(model_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(model_row, 12, 0);
    lv_obj_clear_flag(model_row, LV_OBJ_FLAG_SCROLLABLE);
    s_model_btn[0] = make_model_btn(model_row, kFnModels[0].label, FN_MODEL_PCB110_10);
    s_model_btn[1] = make_model_btn(model_row, kFnModels[1].label, FN_MODEL_PCB085_16);

    lv_obj_t *reference_wrap = lv_obj_create(s_overlay);
    lv_obj_remove_style_all(reference_wrap);
    lv_obj_add_flag(reference_wrap, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(reference_wrap, LV_DIR_VER);
    lv_obj_set_width(reference_wrap, LV_PCT(100));
    lv_obj_set_flex_grow(reference_wrap, 1);

    s_reference_lbl = lv_label_create(reference_wrap);
    lv_label_set_text(s_reference_lbl, "");
    lv_obj_set_style_text_color(s_reference_lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(s_reference_lbl, &lv_font_montserrat_18, 0);
    lv_obj_set_width(s_reference_lbl, LV_PCT(100));

    lv_obj_t *nav_row = lv_obj_create(s_overlay);
    lv_obj_remove_style_all(nav_row);
    lv_obj_set_width(nav_row, LV_PCT(100));
    lv_obj_set_height(nav_row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(nav_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(nav_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(nav_row, 12, 0);
    lv_obj_clear_flag(nav_row, LV_OBJ_FLAG_SCROLLABLE);

    struct NavButtonDef
    {
        const char *text;
        lv_event_cb_t cb;
    };
    const NavButtonDef navButtons[] = {
        {"Mark Outputs", on_mark_nav_clicked},
        {"Address Banks", on_banks_nav_clicked},
        {"Session Log", on_log_nav_clicked},
    };
    for (const auto &def : navButtons)
    {
        lv_obj_t *btn = lv_button_create(nav_row);
        lv_obj_set_size(btn, 220, 60);
        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, def.text);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_24, 0);
        lv_obj_center(lbl);
        lv_obj_add_event_cb(btn, def.cb, LV_EVENT_CLICKED, nullptr);
    }

    // ---- Mark Outputs sub-page ----

    s_mark_overlay = make_full_screen_overlay();
    make_header(s_mark_overlay, "Mark Outputs", on_mark_back_clicked);

    lv_obj_t *mark_caption = lv_label_create(s_mark_overlay);
    lv_label_set_text(mark_caption, "Tap to log what you just did on the real board - nothing is transmitted.");
    lv_obj_set_style_text_color(mark_caption, lv_palette_darken(LV_PALETTE_GREY, 1), 0);
    lv_obj_set_style_text_font(mark_caption, &lv_font_montserrat_18, 0);
    lv_obj_set_width(mark_caption, LV_PCT(100));

    lv_obj_t *mark_grid = lv_obj_create(s_mark_overlay);
    lv_obj_remove_style_all(mark_grid);
    lv_obj_set_width(mark_grid, LV_PCT(100));
    lv_obj_set_height(mark_grid, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(mark_grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_column(mark_grid, 10, 0);
    lv_obj_set_style_pad_row(mark_grid, 10, 0);
    lv_obj_clear_flag(mark_grid, LV_OBJ_FLAG_SCROLLABLE);
    for (int i = 0; i < kFnMaxOutputs; i++)
        s_mark_output_btn[i] = make_mark_btn(mark_grid, i);

    lv_obj_t *mark_all_off_btn = lv_button_create(s_mark_overlay);
    lv_obj_set_size(mark_all_off_btn, 220, 56);
    lv_obj_t *mark_all_off_lbl = lv_label_create(mark_all_off_btn);
    lv_label_set_text(mark_all_off_lbl, "All Marked Off");
    lv_obj_set_style_text_font(mark_all_off_lbl, &lv_font_montserrat_24, 0);
    lv_obj_center(mark_all_off_lbl);
    lv_obj_add_event_cb(mark_all_off_btn, on_mark_all_off_clicked, LV_EVENT_CLICKED, nullptr);

    s_mark_analog_row = lv_obj_create(s_mark_overlay);
    lv_obj_remove_style_all(s_mark_analog_row);
    lv_obj_set_width(s_mark_analog_row, LV_PCT(100));
    lv_obj_set_height(s_mark_analog_row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(s_mark_analog_row, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_mark_analog_row, 6, 0);
    lv_obj_clear_flag(s_mark_analog_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *mark_analog_title_row = lv_obj_create(s_mark_analog_row);
    lv_obj_remove_style_all(mark_analog_title_row);
    lv_obj_set_width(mark_analog_title_row, LV_PCT(100));
    lv_obj_set_height(mark_analog_title_row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(mark_analog_title_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(mark_analog_title_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(mark_analog_title_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *mark_analog_title = lv_label_create(mark_analog_title_row);
    lv_label_set_text(mark_analog_title, "Observed Analog (4-20mA)");
    lv_obj_set_style_text_color(mark_analog_title, lv_color_white(), 0);
    lv_obj_set_style_text_font(mark_analog_title, &lv_font_montserrat_18, 0);

    s_mark_analog_value_lbl = lv_label_create(mark_analog_title_row);
    lv_label_set_text(s_mark_analog_value_lbl, "0%  (4.0 mA)");
    lv_obj_set_style_text_color(s_mark_analog_value_lbl, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_obj_set_style_text_font(s_mark_analog_value_lbl, &lv_font_montserrat_18, 0);

    lv_obj_t *mark_analog_slider_row = lv_obj_create(s_mark_analog_row);
    lv_obj_remove_style_all(mark_analog_slider_row);
    lv_obj_set_width(mark_analog_slider_row, LV_PCT(100));
    lv_obj_set_height(mark_analog_slider_row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(mark_analog_slider_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(mark_analog_slider_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(mark_analog_slider_row, 16, 0);
    lv_obj_clear_flag(mark_analog_slider_row, LV_OBJ_FLAG_SCROLLABLE);

    s_mark_analog_slider = lv_slider_create(mark_analog_slider_row);
    lv_obj_set_flex_grow(s_mark_analog_slider, 1);
    lv_slider_set_range(s_mark_analog_slider, 0, 100);
    lv_slider_set_value(s_mark_analog_slider, 0, LV_ANIM_OFF);
    lv_obj_add_event_cb(s_mark_analog_slider, on_mark_analog_value_changed, LV_EVENT_VALUE_CHANGED, nullptr);

    lv_obj_t *mark_analog_log_btn = lv_button_create(mark_analog_slider_row);
    lv_obj_set_size(mark_analog_log_btn, 140, 52);
    lv_obj_t *mark_analog_log_lbl = lv_label_create(mark_analog_log_btn);
    lv_label_set_text(mark_analog_log_lbl, "Log Value");
    lv_obj_set_style_text_font(mark_analog_log_lbl, &lv_font_montserrat_18, 0);
    lv_obj_center(mark_analog_log_lbl);
    lv_obj_add_event_cb(mark_analog_log_btn, on_mark_analog_log_clicked, LV_EVENT_CLICKED, nullptr);

    s_mark_status_lbl = lv_label_create(s_mark_overlay);
    lv_label_set_text(s_mark_status_lbl, "");
    lv_obj_set_style_text_color(s_mark_status_lbl, lv_palette_main(LV_PALETTE_GREEN), 0);
    lv_obj_set_style_text_font(s_mark_status_lbl, &lv_font_montserrat_18, 0);
    lv_obj_set_width(s_mark_status_lbl, LV_PCT(100));

    // ---- Session Log sub-page ----

    s_log_overlay = make_full_screen_overlay();
    make_header(s_log_overlay, "Session Log", on_log_back_clicked);

    lv_obj_t *filename_row = lv_obj_create(s_log_overlay);
    lv_obj_remove_style_all(filename_row);
    lv_obj_set_width(filename_row, LV_PCT(100));
    lv_obj_set_height(filename_row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(filename_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(filename_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(filename_row, 10, 0);

    lv_obj_t *filename_lbl = lv_label_create(filename_row);
    lv_label_set_text(filename_lbl, "Saleae File:");
    lv_obj_set_style_text_color(filename_lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(filename_lbl, &lv_font_montserrat_18, 0);

    s_log_filename_ta = lv_textarea_create(filename_row);
    lv_obj_set_flex_grow(s_log_filename_ta, 1);
    lv_textarea_set_one_line(s_log_filename_ta, true);
    lv_textarea_set_placeholder_text(s_log_filename_ta, "e.g. digital_20260901_1430.csv");
    lv_obj_set_style_text_font(s_log_filename_ta, &lv_font_montserrat_18, 0);
    lv_obj_add_event_cb(s_log_filename_ta, on_filename_focused, LV_EVENT_FOCUSED, nullptr);

    lv_obj_t *log_btn_row = lv_obj_create(s_log_overlay);
    lv_obj_remove_style_all(log_btn_row);
    lv_obj_set_width(log_btn_row, LV_PCT(100));
    lv_obj_set_height(log_btn_row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(log_btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(log_btn_row, 16, 0);
    lv_obj_clear_flag(log_btn_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *save_btn = lv_button_create(log_btn_row);
    lv_obj_set_flex_grow(save_btn, 1);
    lv_obj_set_height(save_btn, 56);
    lv_obj_t *save_lbl = lv_label_create(save_btn);
    lv_label_set_text(save_lbl, "Save to SD");
    lv_obj_set_style_text_font(save_lbl, &lv_font_montserrat_24, 0);
    lv_obj_center(save_lbl);
    lv_obj_add_event_cb(save_btn, on_save_log_clicked, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *clear_btn = lv_button_create(log_btn_row);
    lv_obj_set_flex_grow(clear_btn, 1);
    lv_obj_set_height(clear_btn, 56);
    lv_obj_t *clear_lbl = lv_label_create(clear_btn);
    lv_label_set_text(clear_lbl, "Clear Log");
    lv_obj_set_style_text_font(clear_lbl, &lv_font_montserrat_24, 0);
    lv_obj_center(clear_lbl);
    lv_obj_add_event_cb(clear_btn, on_clear_log_clicked, LV_EVENT_CLICKED, nullptr);

    s_log_save_status_lbl = lv_label_create(s_log_overlay);
    lv_label_set_text(s_log_save_status_lbl, "");
    lv_obj_set_style_text_font(s_log_save_status_lbl, &lv_font_montserrat_18, 0);
    lv_obj_set_width(s_log_save_status_lbl, LV_PCT(100));

    s_log_wrap = lv_obj_create(s_log_overlay);
    lv_obj_remove_style_all(s_log_wrap);
    lv_obj_add_flag(s_log_wrap, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_log_wrap, LV_DIR_VER);
    lv_obj_set_flex_flow(s_log_wrap, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_log_wrap, 4, 0);
    lv_obj_set_width(s_log_wrap, LV_PCT(100));
    lv_obj_set_flex_grow(s_log_wrap, 1);

    s_log_empty_lbl = lv_label_create(s_log_wrap);
    lv_label_set_text(s_log_empty_lbl, "Nothing logged yet - mark an output or analog value first");
    lv_obj_set_style_text_color(s_log_empty_lbl, lv_palette_main(LV_PALETTE_GREY), 0);
    lv_obj_set_style_text_font(s_log_empty_lbl, &lv_font_montserrat_18, 0);

    for (int i = 0; i < kMaxCaptureLog; i++)
    {
        lv_obj_t *row = lv_label_create(s_log_wrap);
        lv_label_set_text(row, "");
        lv_obj_set_style_text_color(row, lv_color_white(), 0);
        lv_obj_set_style_text_font(row, &lv_font_montserrat_18, 0);
        lv_obj_set_width(row, LV_PCT(100));
        lv_obj_add_flag(row, LV_OBJ_FLAG_HIDDEN);
        s_log_rows[i] = row;
    }

    // ---- Address Banks sub-page (list) ----

    s_banks_overlay = make_full_screen_overlay();
    make_header(s_banks_overlay, "Address Banks", on_banks_back_clicked);

    lv_obj_t *banks_caption = lv_label_create(s_banks_overlay);
    lv_label_set_text(banks_caption,
                       "The discovered/edited address->output map for this model - what a future "
                       "encoder would need to simulate an output by name. Tap a bank to edit it.");
    lv_obj_set_style_text_color(banks_caption, lv_palette_darken(LV_PALETTE_GREY, 1), 0);
    lv_obj_set_style_text_font(banks_caption, &lv_font_montserrat_18, 0);
    lv_obj_set_width(banks_caption, LV_PCT(100));

    lv_obj_t *banks_btn_row = lv_obj_create(s_banks_overlay);
    lv_obj_remove_style_all(banks_btn_row);
    lv_obj_set_width(banks_btn_row, LV_PCT(100));
    lv_obj_set_height(banks_btn_row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(banks_btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(banks_btn_row, 16, 0);
    lv_obj_clear_flag(banks_btn_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *add_bank_btn = lv_button_create(banks_btn_row);
    lv_obj_set_flex_grow(add_bank_btn, 1);
    lv_obj_set_height(add_bank_btn, 56);
    lv_obj_t *add_bank_lbl = lv_label_create(add_bank_btn);
    lv_label_set_text(add_bank_lbl, "Add Bank");
    lv_obj_set_style_text_font(add_bank_lbl, &lv_font_montserrat_24, 0);
    lv_obj_center(add_bank_lbl);
    lv_obj_add_event_cb(add_bank_btn, on_add_bank_clicked, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *save_profile_btn = lv_button_create(banks_btn_row);
    lv_obj_set_flex_grow(save_profile_btn, 1);
    lv_obj_set_height(save_profile_btn, 56);
    lv_obj_t *save_profile_lbl = lv_label_create(save_profile_btn);
    lv_label_set_text(save_profile_lbl, "Save Profile");
    lv_obj_set_style_text_font(save_profile_lbl, &lv_font_montserrat_24, 0);
    lv_obj_center(save_profile_lbl);
    lv_obj_add_event_cb(save_profile_btn, on_save_profile_clicked, LV_EVENT_CLICKED, nullptr);

    s_banks_status_lbl = lv_label_create(s_banks_overlay);
    lv_label_set_text(s_banks_status_lbl, "");
    lv_obj_set_style_text_font(s_banks_status_lbl, &lv_font_montserrat_18, 0);
    lv_obj_set_width(s_banks_status_lbl, LV_PCT(100));

    lv_obj_t *banks_wrap = lv_obj_create(s_banks_overlay);
    lv_obj_remove_style_all(banks_wrap);
    lv_obj_add_flag(banks_wrap, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(banks_wrap, LV_DIR_VER);
    lv_obj_set_flex_flow(banks_wrap, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(banks_wrap, 6, 0);
    lv_obj_set_width(banks_wrap, LV_PCT(100));
    lv_obj_set_flex_grow(banks_wrap, 1);

    s_banks_empty_lbl = lv_label_create(banks_wrap);
    lv_label_set_text(s_banks_empty_lbl, "No banks recorded yet for this model - tap Add Bank");
    lv_obj_set_style_text_color(s_banks_empty_lbl, lv_palette_main(LV_PALETTE_GREY), 0);
    lv_obj_set_style_text_font(s_banks_empty_lbl, &lv_font_montserrat_18, 0);

    for (int i = 0; i < kFnMaxBanks; i++)
    {
        lv_obj_t *row = lv_button_create(banks_wrap);
        lv_obj_set_width(row, LV_PCT(100));
        lv_obj_set_height(row, LV_SIZE_CONTENT);
        lv_obj_add_event_cb(row, on_bank_row_clicked, LV_EVENT_CLICKED,
                             reinterpret_cast<void *>(static_cast<intptr_t>(i)));
        lv_obj_t *lbl = lv_label_create(row);
        lv_label_set_text(lbl, "");
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_18, 0);
        lv_obj_set_width(lbl, LV_PCT(100));
        lv_obj_add_flag(row, LV_OBJ_FLAG_HIDDEN);
        s_bank_rows[i] = row; // the row button is what's shown/hidden; its text lives on the child label (index 0)
    }

    // ---- Edit Bank sub-page ----

    s_bank_edit_overlay = make_full_screen_overlay();
    // make_header()'s title label is its 2nd child (index 1) - back button
    // first, title label second - grabbed here so refresh_bank_editor() can
    // update it to show the bank's address.
    lv_obj_t *bank_edit_header = make_header(s_bank_edit_overlay, "", on_bank_edit_back_clicked);
    s_bank_edit_title_lbl = lv_obj_get_child(bank_edit_header, 1);

    lv_obj_t *kind_row = lv_obj_create(s_bank_edit_overlay);
    lv_obj_remove_style_all(kind_row);
    lv_obj_set_width(kind_row, LV_PCT(100));
    lv_obj_set_height(kind_row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(kind_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(kind_row, 10, 0);
    lv_obj_clear_flag(kind_row, LV_OBJ_FLAG_SCROLLABLE);

    const char *kindLabels[3] = {"Digital", "Analog", "Unknown"};
    for (int i = 0; i < 3; i++)
    {
        lv_obj_t *btn = lv_button_create(kind_row);
        lv_obj_set_flex_grow(btn, 1);
        lv_obj_set_height(btn, 52);
        lv_obj_add_event_cb(btn, on_kind_clicked, LV_EVENT_CLICKED, reinterpret_cast<void *>(static_cast<intptr_t>(i)));
        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, kindLabels[i]);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_18, 0);
        lv_obj_center(lbl);
        s_kind_btn[i] = btn;
    }

    for (int i = 0; i < 4; i++)
    {
        lv_obj_t *row = lv_obj_create(s_bank_edit_overlay);
        lv_obj_remove_style_all(row);
        lv_obj_set_width(row, LV_PCT(100));
        lv_obj_set_height(row, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(row, 10, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        s_slot_row[i] = row;

        lv_obj_t *d_lbl = lv_label_create(row);
        lv_label_set_text(d_lbl, "D1:");
        lv_obj_set_style_text_color(d_lbl, lv_color_white(), 0);
        lv_obj_set_style_text_font(d_lbl, &lv_font_montserrat_18, 0);
        lv_obj_set_width(d_lbl, 60);
        s_slot_d_lbl[i] = d_lbl;

        lv_obj_t *name_btn = lv_button_create(row);
        lv_obj_set_flex_grow(name_btn, 1);
        lv_obj_set_height(name_btn, 52);
        lv_obj_add_event_cb(name_btn, on_slot_name_clicked, LV_EVENT_CLICKED, reinterpret_cast<void *>(static_cast<intptr_t>(i)));
        lv_obj_t *name_lbl = lv_label_create(name_btn);
        lv_label_set_text(name_lbl, "(tap to name)");
        lv_obj_set_style_text_font(name_lbl, &lv_font_montserrat_18, 0);
        lv_obj_align(name_lbl, LV_ALIGN_LEFT_MID, 0, 0);
        s_slot_name_lbl[i] = name_lbl;

        lv_obj_t *conf_btn = lv_button_create(row);
        lv_obj_set_size(conf_btn, 150, 52);
        lv_obj_add_event_cb(conf_btn, on_slot_conf_clicked, LV_EVENT_CLICKED, reinterpret_cast<void *>(static_cast<intptr_t>(i)));
        lv_obj_t *conf_lbl = lv_label_create(conf_btn);
        lv_label_set_text(conf_lbl, "UNKNOWN");
        lv_obj_set_style_text_font(conf_lbl, &lv_font_montserrat_18, 0);
        lv_obj_center(conf_lbl);
        s_slot_conf_btn[i] = conf_btn;
    }

    s_unknown_note_lbl = lv_label_create(s_bank_edit_overlay);
    lv_label_set_text(s_unknown_note_lbl,
                       "Address recorded but not yet characterized. Switch to Digital or Analog "
                       "once you know what it represents.");
    lv_obj_set_style_text_color(s_unknown_note_lbl, lv_palette_darken(LV_PALETTE_GREY, 1), 0);
    lv_obj_set_style_text_font(s_unknown_note_lbl, &lv_font_montserrat_18, 0);
    lv_obj_set_width(s_unknown_note_lbl, LV_PCT(100));

    lv_obj_t *edit_spacer = lv_obj_create(s_bank_edit_overlay);
    lv_obj_remove_style_all(edit_spacer);
    lv_obj_set_width(edit_spacer, LV_PCT(100));
    lv_obj_set_flex_grow(edit_spacer, 1);
    lv_obj_clear_flag(edit_spacer, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *delete_bank_btn = lv_button_create(s_bank_edit_overlay);
    lv_obj_set_size(delete_bank_btn, LV_PCT(100), 56);
    lv_obj_set_style_bg_color(delete_bank_btn, lv_palette_main(LV_PALETTE_RED), 0);
    lv_obj_t *delete_bank_lbl = lv_label_create(delete_bank_btn);
    lv_label_set_text(delete_bank_lbl, "Delete Bank");
    lv_obj_set_style_text_font(delete_bank_lbl, &lv_font_montserrat_24, 0);
    lv_obj_center(delete_bank_lbl);
    lv_obj_add_event_cb(delete_bank_btn, on_delete_bank_clicked, LV_EVENT_CLICKED, nullptr);

    // ---- Shared text-entry keyboard overlay ----

    s_text_entry_overlay = lv_obj_create(lv_layer_top());
    lv_obj_add_flag(s_text_entry_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_size(s_text_entry_overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_text_entry_overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_text_entry_overlay, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(s_text_entry_overlay, 16, 0);
    lv_obj_set_flex_flow(s_text_entry_overlay, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_text_entry_overlay, 12, 0);
    lv_obj_clear_flag(s_text_entry_overlay, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *text_entry_header = lv_obj_create(s_text_entry_overlay);
    lv_obj_remove_style_all(text_entry_header);
    lv_obj_set_width(text_entry_header, LV_PCT(100));
    lv_obj_set_height(text_entry_header, 56);
    lv_obj_clear_flag(text_entry_header, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *text_entry_cancel_btn = lv_button_create(text_entry_header);
    lv_obj_set_size(text_entry_cancel_btn, 120, 56);
    lv_obj_align(text_entry_cancel_btn, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_t *text_entry_cancel_lbl = lv_label_create(text_entry_cancel_btn);
    lv_label_set_text(text_entry_cancel_lbl, "Cancel");
    lv_obj_set_style_text_font(text_entry_cancel_lbl, &lv_font_montserrat_24, 0);
    lv_obj_center(text_entry_cancel_lbl);
    lv_obj_add_event_cb(text_entry_cancel_btn, on_text_entry_cancel_clicked, LV_EVENT_CLICKED, nullptr);

    s_text_entry_title_lbl = lv_label_create(text_entry_header);
    lv_label_set_text(s_text_entry_title_lbl, "");
    lv_obj_set_style_text_color(s_text_entry_title_lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(s_text_entry_title_lbl, &lv_font_montserrat_28, 0);
    lv_obj_align(s_text_entry_title_lbl, LV_ALIGN_CENTER, 0, 0);

    s_text_entry_ta = lv_textarea_create(s_text_entry_overlay);
    lv_obj_set_width(s_text_entry_ta, LV_PCT(100));
    lv_textarea_set_one_line(s_text_entry_ta, true);
    lv_obj_set_style_text_font(s_text_entry_ta, &lv_font_montserrat_28, 0);

    s_text_entry_keyboard = lv_keyboard_create(s_text_entry_overlay);
    lv_keyboard_set_textarea(s_text_entry_keyboard, s_text_entry_ta);
    lv_obj_set_flex_grow(s_text_entry_keyboard, 1);
    lv_obj_add_event_cb(s_text_entry_ta, on_text_entry_ready, LV_EVENT_READY, nullptr);

    apply_model_to_ui();
}

void ui_capture_learn_show()
{
    apply_model_to_ui();
    lv_obj_clear_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
}
