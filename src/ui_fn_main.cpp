#include "ui_fn_main.h"

#include <Arduino.h>
#include <cstdio>

#include "espnow_state.h"
#include "fn_output_model.h"

namespace
{
    lv_obj_t *s_overlay = nullptr;
    lv_obj_t *s_pod_label = nullptr;
    lv_obj_t *s_sim_switch = nullptr;
    lv_obj_t *s_state_label = nullptr;
    lv_obj_t *s_output_led[kFnMaxOutputs] = {nullptr};
    lv_obj_t *s_analog_lbl = nullptr;
    lv_obj_t *s_raw_lbl = nullptr;

    bool s_visible = false;

    // Optimistic local belief for the switch's own state only (same
    // fire-and-forget reasoning as ui_fn_output.cpp's s_tx_running) - the
    // pod's actual decoded output/profile state always comes from
    // espnow_last_fn_main_status(), never guessed locally.
    bool s_sim_enabled = false;

    // Names transcribed from docs/PCB085_ANALYSIS.md's "Known Output
    // Functions" table (CONFIRMED) for outputs 1-8, except Output 7 -
    // relabeled "PCT" per operator instruction (site-specific naming, not
    // from the doc, which calls it "Isolation Valve"). Outputs 9-16 have
    // no confirmed function yet (that doc's section 15), so they stay
    // generic rather than guessing.
    constexpr const char *kOutputLabel[kFnMaxOutputs] = {
        "1 - Alarm", "2 - Valve 1", "3 - Valve 2", "4 - PB",
        "5 - RB", "6 - RH", "7 - PCT", "8 - PH",
        "Out 9", "Out 10", "Out 11", "Out 12",
        "Out 13", "Out 14", "Out 15", "Out 16",
    };

    // Per-output "on" color - purely a display convention (e.g. red for
    // alarm/heat), not a protocol distinction. Unassigned/unconfirmed
    // outputs (8-15) default to green.
    lv_color_t output_on_color(int index)
    {
        switch (index)
        {
        case 0: // Alarm
        case 5: // Regen Heater
        case 7: // Process Heater
            return lv_palette_main(LV_PALETTE_RED);
        case 1: // Valve 1
        case 2: // Valve 2
            return lv_color_hex(0xDAA520); // goldenrod - requested in place of plain yellow
        case 3: // Process Blower
        case 4: // Regen Blower
            return lv_palette_main(LV_PALETTE_BLUE);
        case 6: // Isolation Valve
            return lv_palette_main(LV_PALETTE_PURPLE);
        default:
            return lv_palette_main(LV_PALETTE_GREEN);
        }
    }

    void send_command(const char *name)
    {
        int pod_index = espnow_find_fn_pod_bridge_index();
        if (pod_index < 0)
            return; // caller UI should already be disabled in this case, but don't trust UI state alone
        espnow_send_command(pod_index, name);
    }

    // Renders a "10001"-style bit string, MSB-first - raw address bits
    // should never be hidden behind only the human-readable interpretation
    // (FN_OUTPUT_Tester_Handoff/CLAUDE.md's decoder rendering rule).
    void format_address_bits(uint8_t bits5, char *out, size_t outSize)
    {
        for (int i = 0; i < 5 && i + 1 < static_cast<int>(outSize); i++)
            out[i] = (bits5 & (1u << (4 - i))) ? '1' : '0';
        out[5] = '\0';
    }

    void set_led(int index, bool on)
    {
        lv_obj_set_style_bg_color(s_output_led[index], on ? output_on_color(index) : lv_palette_darken(LV_PALETTE_GREY, 2),
                                   0);
    }

    void refresh()
    {
        int pod_index = espnow_find_fn_pod_bridge_index();

        if (pod_index < 0)
        {
            lv_label_set_text(s_pod_label, "No FN pod paired.\nPair one from the ESP NOW page first.");
            lv_obj_set_style_text_color(s_pod_label, lv_palette_main(LV_PALETTE_ORANGE), 0);
            lv_label_set_text(s_state_label, "");
            lv_label_set_text(s_analog_lbl, "");
            lv_label_set_text(s_raw_lbl, "");
            lv_obj_add_state(s_sim_switch, LV_STATE_DISABLED);
            for (int i = 0; i < kFnMaxOutputs; i++)
                set_led(i, false);
            return;
        }

        const SM_PairedBridge *bridge = espnow_paired_bridge(pod_index);
        lv_label_set_text_fmt(s_pod_label, "Pod: %s (%02X:%02X:%02X:%02X:%02X:%02X)",
                               bridge->friendlyName, bridge->mac[0], bridge->mac[1], bridge->mac[2],
                               bridge->mac[3], bridge->mac[4], bridge->mac[5]);
        lv_obj_set_style_text_color(s_pod_label, lv_color_white(), 0);
        lv_obj_clear_state(s_sim_switch, LV_STATE_DISABLED);

        // If the pod's gone quiet (broken link/out of range), don't let
        // this screen keep claiming Simulate is on - auto-exit and reflect
        // it in the switch itself, not just the status text, so the UI
        // never lies about state. Mirrors the pod's own independent
        // link-lost auto-exit (M5AtomS3-FN-Bridge/src/main.cpp's
        // cyd_link_lost()) - each side detects loss locally off its own
        // timeout and exits unilaterally, deliberately not coordinated via
        // a message that might not get through a broken link.
        constexpr uint32_t kLinkLostMs = 25000; // matches the pod's own kCydLinkLostTimeoutMs
        if (s_sim_enabled && !espnow_device_seen_within(bridge->deviceID, kLinkLostMs))
        {
            s_sim_enabled = false;
            lv_obj_clear_state(s_sim_switch, LV_STATE_CHECKED);
        }

        // The pod broadcasts its actual decode snapshot (SM_FN_MAIN_STATUS)
        // whenever it changes - only trust it if it's from *this* paired
        // pod and recent enough to plausibly still be current, the same
        // freshness reasoning as ui_fn_output.cpp's FN TX status read.
        const SM_FnMainStatus *status = espnow_last_fn_main_status();
        constexpr uint32_t kStatusFreshMs = 5000;
        bool haveFreshStatus =
            status != nullptr && status->deviceID == bridge->deviceID && millis() - status->receivedMs < kStatusFreshMs;

        if (!s_sim_enabled)
        {
            lv_label_set_text(s_state_label, "Simulation disabled - pod's button acts as a normal ping");
            lv_obj_set_style_text_color(s_state_label, lv_palette_darken(LV_PALETTE_GREY, 1), 0);
            lv_label_set_text(s_analog_lbl, "");
            lv_label_set_text(s_raw_lbl, "");
            for (int i = 0; i < kFnMaxOutputs; i++)
                set_led(i, false);
            return;
        }

        if (!haveFreshStatus || !status->simulating)
        {
            lv_label_set_text(s_state_label, "Simulation armed - starting playback...");
            lv_obj_set_style_text_color(s_state_label, lv_palette_main(LV_PALETTE_BLUE), 0);
            lv_label_set_text(s_analog_lbl, "");
            lv_label_set_text(s_raw_lbl, "");
            for (int i = 0; i < kFnMaxOutputs; i++)
                set_led(i, false);
            return;
        }

        // Actively simulating and we have a fresh status.
        for (int i = 0; i < kFnMaxOutputs; i++)
            set_led(i, status->outputs[i]);

        char addrStr[6];
        format_address_bits(status->lastAddressBits, addrStr, sizeof(addrStr));

        if (status->profileMatch == FN_MAIN_PROFILE_PCB085)
        {
            lv_label_set_text_fmt(s_state_label, "Simulating \"%s\" - PCB-085 pattern recognized",
                                   status->captureLabel);
            lv_obj_set_style_text_color(s_state_label, lv_palette_main(LV_PALETTE_GREEN), 0);
        }
        else if (status->profileMatch == FN_MAIN_PROFILE_UNRECOGNIZED)
        {
            lv_label_set_text_fmt(s_state_label, "Simulating \"%s\" - not a recognized PCB-085 pattern",
                                   status->captureLabel);
            lv_obj_set_style_text_color(s_state_label, lv_palette_main(LV_PALETTE_ORANGE), 0);
        }
        else
        {
            lv_label_set_text_fmt(s_state_label, "Simulating \"%s\" - waiting for the first decoded word...",
                                   status->captureLabel);
            lv_obj_set_style_text_color(s_state_label, lv_palette_main(LV_PALETTE_BLUE), 0);
        }

        // Only shown while Output 8 (PH) is on - operator instruction, not
        // a documented PCB085_ANALYSIS.md dependency: the analog reading
        // isn't considered meaningful to display unless PH is enabled.
        if (status->outputs[7])
        {
            float milliamps = 4.0f + (status->analogCode / 15.0f) * 16.0f; // 4-20mA span - see PCB085_ANALYSIS.md
            // lv_label_set_text_fmt() goes through LVGL's own lv_snprintf(),
            // which doesn't support %f when LV_USE_FLOAT is 0 (this
            // project's lv_conf.h) - silently drops the value and prints a
            // bare "f" instead of corrupting further. Format with the real
            // (Arduino/newlib) snprintf() first, then hand LVGL a %s.
            char buf[48];
            snprintf(buf, sizeof(buf), "Analog code: %u  (~%.1f mA)", status->analogCode,
                     static_cast<double>(milliamps));
            lv_label_set_text(s_analog_lbl, buf);
        }
        else
        {
            lv_label_set_text(s_analog_lbl, "");
        }
        lv_label_set_text_fmt(s_raw_lbl, "Last address: %s", addrStr);
    }

    void poll(lv_timer_t *)
    {
        if (!s_visible)
            return;
        refresh();
    }

    void on_sim_switch_changed(lv_event_t *e)
    {
        lv_obj_t *sw = static_cast<lv_obj_t *>(lv_event_get_target(e));
        s_sim_enabled = lv_obj_has_state(sw, LV_STATE_CHECKED);
        send_command(s_sim_enabled ? "SIM_ENABLE" : "SIM_DISABLE");
        refresh();
    }

    void on_back_clicked(lv_event_t *)
    {
        s_visible = false;
        lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
    }

    // Read-only LED tile - unlike ui_fn_output.cpp's make_output_led(), this
    // is a plain lv_obj (not a button, not checkable): nothing here is
    // tappable, it only ever reflects state broadcast from the pod.
    lv_obj_t *make_output_led(lv_obj_t *parent, int index)
    {
        lv_obj_t *tile = lv_obj_create(parent);
        lv_obj_remove_style_all(tile);
        lv_obj_set_size(tile, 170, 60);
        lv_obj_set_style_bg_color(tile, lv_palette_darken(LV_PALETTE_GREY, 2), 0);
        lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(tile, 8, 0);
        lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *lbl = lv_label_create(tile);
        lv_label_set_text(lbl, kOutputLabel[index]);
        lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_18, 0);
        lv_obj_center(lbl);
        return tile;
    }
}

void ui_fn_main_build_pool()
{
    s_overlay = lv_obj_create(lv_layer_top());
    lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_size(s_overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_overlay, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(s_overlay, 16, 0);
    lv_obj_set_flex_flow(s_overlay, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_overlay, 8, 0);
    lv_obj_clear_flag(s_overlay, LV_OBJ_FLAG_SCROLLABLE);

    // Manual alignment, not a flex row with space-between - same reasoning
    // as every other page header in this app: the title needs to sit
    // dead-center regardless of the (left-aligned) Back button's width.
    lv_obj_t *header = lv_obj_create(s_overlay);
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
    lv_obj_add_event_cb(back_btn, on_back_clicked, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "FN Main");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *body = lv_obj_create(s_overlay);
    lv_obj_remove_style_all(body);
    lv_obj_set_width(body, LV_PCT(100));
    lv_obj_set_flex_grow(body, 1);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(body, 10, 0);
    lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLLABLE);

    s_pod_label = lv_label_create(body);
    lv_label_set_text(s_pod_label, "");
    lv_obj_set_style_text_font(s_pod_label, &lv_font_montserrat_18, 0);
    lv_obj_set_width(s_pod_label, LV_PCT(100));

    // Simulate toggle - there's no real FN-MAIN receive interface yet (see
    // ui_fn_main.h's top comment), so this is the only way this screen
    // ever sees any decoded data. Analog readout docks to the right of the
    // switch/label group, in the row's otherwise-free space.
    lv_obj_t *sim_row = lv_obj_create(body);
    lv_obj_remove_style_all(sim_row);
    lv_obj_set_width(sim_row, LV_PCT(100));
    lv_obj_set_height(sim_row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(sim_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(sim_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(sim_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *sim_left = lv_obj_create(sim_row);
    lv_obj_remove_style_all(sim_left);
    lv_obj_set_width(sim_left, LV_SIZE_CONTENT);
    lv_obj_set_height(sim_left, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(sim_left, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(sim_left, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(sim_left, 12, 0);
    lv_obj_clear_flag(sim_left, LV_OBJ_FLAG_SCROLLABLE);

    s_sim_switch = lv_switch_create(sim_left);
    lv_obj_set_size(s_sim_switch, 70, 36);
    lv_obj_add_event_cb(s_sim_switch, on_sim_switch_changed, LV_EVENT_VALUE_CHANGED, nullptr);

    lv_obj_t *sim_lbl = lv_label_create(sim_left);
    lv_label_set_text(sim_lbl, "Simulate (replay a captured PCB-085 session)");
    lv_obj_set_style_text_color(sim_lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(sim_lbl, &lv_font_montserrat_18, 0);

    s_analog_lbl = lv_label_create(sim_row);
    lv_label_set_text(s_analog_lbl, "");
    lv_obj_set_style_text_color(s_analog_lbl, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_obj_set_style_text_font(s_analog_lbl, &lv_font_montserrat_18, 0);

    s_state_label = lv_label_create(body);
    lv_label_set_text(s_state_label, "");
    lv_obj_set_style_text_font(s_state_label, &lv_font_montserrat_18, 0);
    lv_obj_set_width(s_state_label, LV_PCT(100));

    // Output LED grid - all kFnMaxOutputs pre-created (this project prefers
    // pre-allocated pools over dynamic add/remove), read-only - see
    // make_output_led()'s comment. Unlike the Output Tester, there's no
    // per-model hide/show here: PCB-110 has no confirmed address map at
    // all, so outputs 9-16 simply never light (no confirmed encoding to
    // recognize them by), rather than being hidden based on an operator-
    // selected model this screen has no reason to ask for.
    lv_obj_t *grid = lv_obj_create(body);
    lv_obj_remove_style_all(grid);
    lv_obj_set_width(grid, LV_PCT(100));
    lv_obj_set_height(grid, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_column(grid, 10, 0);
    lv_obj_set_style_pad_row(grid, 10, 0);
    lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
    for (int i = 0; i < kFnMaxOutputs; i++)
        s_output_led[i] = make_output_led(grid, i);

    // Raw decoded address, per FN_OUTPUT_Tester_Handoff/CLAUDE.md's "never
    // hide the raw address behind only human-readable names" rule.
    s_raw_lbl = lv_label_create(body);
    lv_label_set_text(s_raw_lbl, "");
    lv_obj_set_style_text_color(s_raw_lbl, lv_palette_darken(LV_PALETTE_GREY, 1), 0);
    lv_obj_set_style_text_font(s_raw_lbl, &lv_font_montserrat_18, 0);
    lv_obj_set_width(s_raw_lbl, LV_PCT(100));

    lv_timer_create(poll, 500, nullptr); // faster than ui_fn_output.cpp's 1000ms - decoded state can change quickly during playback
}

void ui_fn_main_show()
{
    s_visible = true;
    refresh();
    lv_obj_clear_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
}
