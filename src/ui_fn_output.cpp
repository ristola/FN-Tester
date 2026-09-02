#include "ui_fn_output.h"

#include <Arduino.h>

#include "app_state.h"
#include "espnow_state.h"
#include "fn_output_model.h"

namespace
{
    lv_obj_t *s_overlay = nullptr;
    lv_obj_t *s_pod_label = nullptr;
    lv_obj_t *s_state_label = nullptr;

    lv_obj_t *s_model_btn[2] = {nullptr, nullptr};
    lv_obj_t *s_enabled_switch = nullptr;
    lv_obj_t *s_output_btn[kFnMaxOutputs] = {nullptr};
    lv_obj_t *s_analog_row = nullptr;
    lv_obj_t *s_analog_slider = nullptr;
    lv_obj_t *s_analog_value_lbl = nullptr;
    lv_obj_t *s_all_off_btn = nullptr;
    lv_obj_t *s_uart_test_btn = nullptr;

    bool s_visible = false;
    bool s_output_state[kFnMaxOutputs] = {false};

    // Optimistic local belief only - every SM_COMMAND this screen sends is
    // fire-and-forget (same as the ESP NOW Diagnostics page's Reboot/
    // Restore actions and this screen's earlier bench-test controls), so
    // these reflect the last action taken here, not a confirmed live status
    // read back from the pod.
    bool s_tx_running = false; // "Outputs Enabled" - continuous FN transmission

    // UART_TEST is self-timed on the pod (auto-stops after
    // kUartTestDurationMs there); mirrored here purely so the button
    // disables itself and the status label can show something sensible for
    // that same window, not because this screen gets any actual completion
    // signal back.
    constexpr uint32_t kUartTestDurationMs = 5000;
    bool s_uart_test_running = false;
    uint32_t s_uart_test_started_ms = 0;

    uint8_t current_model()
    {
        return fn_output_model_clamped(g_config.fn_output_model);
    }

    // First paired bridge that identifies as the FN two-wire pod. Returns -1
    // if none is paired yet (e.g. fresh setup, or the pod isn't the FN kind).
    int find_pod_index()
    {
        int count = espnow_paired_bridge_count();
        for (int i = 0; i < count; i++)
        {
            const SM_PairedBridge *bridge = espnow_paired_bridge(i);
            if (bridge != nullptr && bridge->deviceType == SM_DEVICE_FN_2WIRE_POD)
                return i;
        }
        return -1;
    }

    void send_command(const char *name, int32_t argument = 0)
    {
        int pod_index = find_pod_index();
        if (pod_index < 0)
            return; // caller UI should already be disabled in this case, but don't trust UI state alone
        espnow_send_command(pod_index, name, argument);
    }

    // Shows/hides the output LED grid buttons and the analog row for the
    // currently-selected model, and restyles the two model buttons so the
    // active one is visually highlighted.
    void apply_model_to_ui()
    {
        uint8_t model = current_model();
        const FnModelInfo &info = kFnModels[model];

        for (int i = 0; i < kFnMaxOutputs; i++)
        {
            if (i < info.outputCount)
                lv_obj_clear_flag(s_output_btn[i], LV_OBJ_FLAG_HIDDEN);
            else
                lv_obj_add_flag(s_output_btn[i], LV_OBJ_FLAG_HIDDEN);
        }

        if (info.hasAnalog)
            lv_obj_clear_flag(s_analog_row, LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_add_flag(s_analog_row, LV_OBJ_FLAG_HIDDEN);

        for (int i = 0; i < 2; i++)
        {
            bool active = (i == model);
            lv_obj_set_style_bg_color(s_model_btn[i],
                                       active ? lv_palette_main(LV_PALETTE_BLUE) : lv_palette_darken(LV_PALETTE_GREY, 2),
                                       0);
        }
    }

    // Clears every output's local state/LED and tells the pod, without
    // touching the analog value - "All Outputs Off" is specifically about
    // the discrete outputs, matching FN_OUTPUT_Tester_Handoff/CLAUDE.md's
    // "OUTPUT-BOARD TESTER Mode" list ("support All Off" alongside, not
    // instead of, analog control).
    void clear_all_outputs(bool notifyPod)
    {
        uint8_t count = kFnModels[current_model()].outputCount;
        for (int i = 0; i < count; i++)
        {
            s_output_state[i] = false;
            lv_obj_clear_state(s_output_btn[i], LV_STATE_CHECKED);
        }
        if (notifyPod)
            send_command("ALL_OUTPUTS_OFF");
    }

    void refresh()
    {
        int pod_index = find_pod_index();

        if (pod_index < 0)
        {
            lv_label_set_text(s_pod_label, "No FN pod paired.\nPair one from the ESP NOW page first.");
            lv_obj_set_style_text_color(s_pod_label, lv_palette_main(LV_PALETTE_ORANGE), 0);
            lv_label_set_text(s_state_label, "");

            lv_obj_add_state(s_model_btn[0], LV_STATE_DISABLED);
            lv_obj_add_state(s_model_btn[1], LV_STATE_DISABLED);
            lv_obj_add_state(s_enabled_switch, LV_STATE_DISABLED);
            lv_obj_add_state(s_all_off_btn, LV_STATE_DISABLED);
            lv_obj_add_state(s_uart_test_btn, LV_STATE_DISABLED);
            lv_obj_add_state(s_analog_slider, LV_STATE_DISABLED);
            for (int i = 0; i < kFnMaxOutputs; i++)
                lv_obj_add_state(s_output_btn[i], LV_STATE_DISABLED);
            return;
        }

        const SM_PairedBridge *bridge = espnow_paired_bridge(pod_index);
        lv_label_set_text_fmt(s_pod_label, "Pod: %s (%02X:%02X:%02X:%02X:%02X:%02X)",
                               bridge->friendlyName, bridge->mac[0], bridge->mac[1], bridge->mac[2],
                               bridge->mac[3], bridge->mac[4], bridge->mac[5]);
        lv_obj_set_style_text_color(s_pod_label, lv_color_white(), 0);

        lv_obj_clear_state(s_model_btn[0], LV_STATE_DISABLED);
        lv_obj_clear_state(s_model_btn[1], LV_STATE_DISABLED);
        lv_obj_clear_state(s_all_off_btn, LV_STATE_DISABLED);
        lv_obj_clear_state(s_analog_slider, LV_STATE_DISABLED);
        for (int i = 0; i < kFnMaxOutputs; i++)
            lv_obj_clear_state(s_output_btn[i], LV_STATE_DISABLED);

        // The pod stops the UART test on its own after kUartTestDurationMs -
        // this just clears the local optimistic flag on the same schedule so
        // the button/label don't stay stuck showing "running" forever.
        if (s_uart_test_running && millis() - s_uart_test_started_ms >= kUartTestDurationMs)
            s_uart_test_running = false;

        if (s_uart_test_running)
        {
            lv_obj_add_state(s_enabled_switch, LV_STATE_DISABLED);
            lv_obj_add_state(s_uart_test_btn, LV_STATE_DISABLED);
            lv_label_set_text(s_state_label, "Sending UART decoder test (5s, GPIO2, 9600 8N1)...");
            lv_obj_set_style_text_color(s_state_label, lv_palette_main(LV_PALETTE_BLUE), 0);
        }
        else if (s_tx_running)
        {
            lv_obj_clear_state(s_enabled_switch, LV_STATE_DISABLED);
            lv_obj_clear_state(s_uart_test_btn, LV_STATE_DISABLED);

            // The pod broadcasts its actual FN TX mode (SM_STATUS) whenever
            // it changes - see espnow_state.h's espnow_last_fn_tx_status().
            // Only trust it if it's from *this* paired pod and recent
            // enough to plausibly be describing the current
            // FN_TX_START/SET_MODEL/etc. we just sent, rather than a stale
            // reading from before this screen was opened or before the
            // pod rebooted.
            const SM_FnTxStatus *status = espnow_last_fn_tx_status();
            constexpr uint32_t kStatusFreshMs = 5000;
            bool haveFreshStatus = status != nullptr && status->deviceID == bridge->deviceID &&
                                    millis() - status->receivedMs < kStatusFreshMs;

            if (haveFreshStatus && status->txMode == FN_TX_MODE_REAL_ENCODED)
            {
                lv_label_set_text(s_state_label, "Outputs enabled - transmitting real FN encoding to pod");
                lv_obj_set_style_text_color(s_state_label, lv_palette_main(LV_PALETTE_GREEN), 0);
            }
            else if (haveFreshStatus && status->txMode == FN_TX_MODE_PLACEHOLDER)
            {
                lv_label_set_text(s_state_label,
                                   "Outputs enabled - pod has no confirmed address map for this "
                                   "board yet, transmitting a bench placeholder only");
                lv_obj_set_style_text_color(s_state_label, lv_palette_main(LV_PALETTE_ORANGE), 0);
            }
            else
            {
                lv_label_set_text(s_state_label, "Outputs enabled - continuously transmitting to pod");
                lv_obj_set_style_text_color(s_state_label, lv_palette_main(LV_PALETTE_GREEN), 0);
            }
        }
        else
        {
            lv_obj_clear_state(s_enabled_switch, LV_STATE_DISABLED);
            lv_obj_clear_state(s_uart_test_btn, LV_STATE_DISABLED);
            lv_label_set_text(s_state_label, "Outputs disabled");
            lv_obj_set_style_text_color(s_state_label, lv_palette_darken(LV_PALETTE_GREY, 1), 0);
        }
    }

    void on_model_btn_clicked(lv_event_t *e)
    {
        uint8_t model = static_cast<uint8_t>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
        if (model == current_model())
            return;

        g_config.fn_output_model = model;
        config_save(g_config);

        clear_all_outputs(/*notifyPod=*/false); // switching board profiles - old bitmap doesn't mean anything to the new one
        lv_slider_set_value(s_analog_slider, 0, LV_ANIM_OFF);
        lv_label_set_text(s_analog_value_lbl, "0%  (4.0 mA)");

        apply_model_to_ui();
        send_command("SET_MODEL", model);
    }

    void on_output_btn_clicked(lv_event_t *e)
    {
        int index = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
        lv_obj_t *btn = static_cast<lv_obj_t *>(lv_event_get_target(e));
        bool on = lv_obj_has_state(btn, LV_STATE_CHECKED); // LV_OBJ_FLAG_CHECKABLE already toggled this before the event fires
        s_output_state[index] = on;
        send_command("SET_OUTPUT", (static_cast<int32_t>(index) << 1) | (on ? 1 : 0));
    }

    void on_enabled_switch_changed(lv_event_t *e)
    {
        lv_obj_t *sw = static_cast<lv_obj_t *>(lv_event_get_target(e));
        s_tx_running = lv_obj_has_state(sw, LV_STATE_CHECKED);
        s_uart_test_running = false; // enabling/disabling outputs stops any in-progress UART test on the pod too
        send_command(s_tx_running ? "FN_TX_START" : "FN_TX_STOP");
        refresh();
    }

    void on_analog_value_changed(lv_event_t *e)
    {
        lv_obj_t *slider = static_cast<lv_obj_t *>(lv_event_get_target(e));
        int32_t percent = lv_slider_get_value(slider);
        float milliamps = 4.0f + (percent / 100.0f) * 16.0f; // 4-20mA span - see PCB085_ANALYSIS.md
        lv_label_set_text_fmt(s_analog_value_lbl, "%d%%  (%.1f mA)", static_cast<int>(percent), static_cast<double>(milliamps));
    }

    void on_analog_released(lv_event_t *e)
    {
        lv_obj_t *slider = static_cast<lv_obj_t *>(lv_event_get_target(e));
        int32_t percent = lv_slider_get_value(slider);
        send_command("SET_ANALOG", percent);
    }

    void on_all_off_clicked(lv_event_t *)
    {
        clear_all_outputs(/*notifyPod=*/true);
    }

    void on_uart_test_clicked(lv_event_t *)
    {
        send_command("UART_TEST");
        s_tx_running = false; // UART_TEST stops any in-progress FN transmission on the pod too
        s_uart_test_running = true;
        s_uart_test_started_ms = millis();
        refresh();
    }

    void on_back_clicked(lv_event_t *)
    {
        s_visible = false;
        lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
    }

    void poll(lv_timer_t *)
    {
        if (!s_visible)
            return;
        refresh(); // catches a pod pairing/unpairing while this screen is open
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

    // A checkable "LED" tile for one output - tap toggles it on/off (green
    // when checked). LV_OBJ_FLAG_CHECKABLE makes LVGL flip LV_STATE_CHECKED
    // automatically on tap, before on_output_btn_clicked() runs, so the
    // handler just reads the resulting state rather than computing it.
    lv_obj_t *make_output_led(lv_obj_t *parent, int index)
    {
        lv_obj_t *btn = lv_button_create(parent);
        lv_obj_set_size(btn, 160, 60);
        lv_obj_add_flag(btn, LV_OBJ_FLAG_CHECKABLE);
        lv_obj_set_style_bg_color(btn, lv_palette_darken(LV_PALETTE_GREY, 2), 0);
        lv_obj_set_style_bg_color(btn, lv_palette_main(LV_PALETTE_GREEN), LV_STATE_CHECKED);
        lv_obj_add_event_cb(btn, on_output_btn_clicked, LV_EVENT_CLICKED,
                             reinterpret_cast<void *>(static_cast<intptr_t>(index)));

        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text_fmt(lbl, "Out %d", index + 1);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_24, 0);
        lv_obj_center(lbl);
        return btn;
    }

    lv_obj_t *make_small_btn(lv_obj_t *parent, const char *text, lv_event_cb_t cb)
    {
        lv_obj_t *btn = lv_button_create(parent);
        lv_obj_set_flex_grow(btn, 1);
        lv_obj_set_height(btn, 56);
        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, nullptr);
        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, text);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_24, 0);
        lv_obj_center(lbl);
        return btn;
    }
}

void ui_fn_output_build_pool()
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
    lv_label_set_text(title, "FN Output");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

    // Scrollable as a safety margin against the 16-output layout overflowing
    // vertically - LVGL buttons still register a plain tap over a scrollable
    // parent without needing a drag, so this doesn't fight the LED taps.
    lv_obj_t *body = lv_obj_create(s_overlay);
    lv_obj_remove_style_all(body);
    lv_obj_set_width(body, LV_PCT(100));
    lv_obj_set_flex_grow(body, 1);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(body, 10, 0);

    s_pod_label = lv_label_create(body);
    lv_label_set_text(s_pod_label, "");
    lv_obj_set_style_text_font(s_pod_label, &lv_font_montserrat_18, 0);
    lv_obj_set_width(s_pod_label, LV_PCT(100));

    // Model picker.
    lv_obj_t *model_row = lv_obj_create(body);
    lv_obj_remove_style_all(model_row);
    lv_obj_set_width(model_row, LV_PCT(100));
    lv_obj_set_height(model_row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(model_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(model_row, 12, 0);
    lv_obj_clear_flag(model_row, LV_OBJ_FLAG_SCROLLABLE);
    s_model_btn[0] = make_model_btn(model_row, kFnModels[0].label, FN_MODEL_PCB110_10);
    s_model_btn[1] = make_model_btn(model_row, kFnModels[1].label, FN_MODEL_PCB085_16);

    // Outputs Enabled.
    lv_obj_t *enabled_row = lv_obj_create(body);
    lv_obj_remove_style_all(enabled_row);
    lv_obj_set_width(enabled_row, LV_PCT(100));
    lv_obj_set_height(enabled_row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(enabled_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(enabled_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(enabled_row, 12, 0);
    lv_obj_clear_flag(enabled_row, LV_OBJ_FLAG_SCROLLABLE);

    s_enabled_switch = lv_switch_create(enabled_row);
    lv_obj_set_size(s_enabled_switch, 70, 36);
    lv_obj_add_event_cb(s_enabled_switch, on_enabled_switch_changed, LV_EVENT_VALUE_CHANGED, nullptr);

    lv_obj_t *enabled_lbl = lv_label_create(enabled_row);
    lv_label_set_text(enabled_lbl, "Outputs Enabled (continuous transmission)");
    lv_obj_set_style_text_color(enabled_lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(enabled_lbl, &lv_font_montserrat_18, 0);

    s_state_label = lv_label_create(body);
    lv_label_set_text(s_state_label, "");
    lv_obj_set_style_text_font(s_state_label, &lv_font_montserrat_18, 0);
    lv_obj_set_width(s_state_label, LV_PCT(100));

    // Output LED grid - all kFnMaxOutputs pre-created (this project prefers
    // pre-allocated pools over dynamic add/remove - see PROJECT_MEMORY.md's
    // "First-render fragility" note), extras hidden per-model in
    // apply_model_to_ui().
    lv_obj_t *grid = lv_obj_create(body);
    lv_obj_remove_style_all(grid);
    lv_obj_set_width(grid, LV_PCT(100));
    lv_obj_set_height(grid, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_column(grid, 10, 0);
    lv_obj_set_style_pad_row(grid, 10, 0);
    lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
    for (int i = 0; i < kFnMaxOutputs; i++)
        s_output_btn[i] = make_output_led(grid, i);

    // Analog (PCB-085 only).
    s_analog_row = lv_obj_create(body);
    lv_obj_remove_style_all(s_analog_row);
    lv_obj_set_width(s_analog_row, LV_PCT(100));
    lv_obj_set_height(s_analog_row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(s_analog_row, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_analog_row, 6, 0);
    lv_obj_clear_flag(s_analog_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *analog_title_row = lv_obj_create(s_analog_row);
    lv_obj_remove_style_all(analog_title_row);
    lv_obj_set_width(analog_title_row, LV_PCT(100));
    lv_obj_set_height(analog_title_row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(analog_title_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(analog_title_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(analog_title_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *analog_title = lv_label_create(analog_title_row);
    lv_label_set_text(analog_title, "Analog Output (4-20mA)");
    lv_obj_set_style_text_color(analog_title, lv_color_white(), 0);
    lv_obj_set_style_text_font(analog_title, &lv_font_montserrat_18, 0);

    s_analog_value_lbl = lv_label_create(analog_title_row);
    lv_label_set_text(s_analog_value_lbl, "0%  (4.0 mA)");
    lv_obj_set_style_text_color(s_analog_value_lbl, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_obj_set_style_text_font(s_analog_value_lbl, &lv_font_montserrat_18, 0);

    s_analog_slider = lv_slider_create(s_analog_row);
    lv_obj_set_width(s_analog_slider, LV_PCT(100));
    lv_slider_set_range(s_analog_slider, 0, 100);
    lv_slider_set_value(s_analog_slider, 0, LV_ANIM_OFF);
    lv_obj_add_event_cb(s_analog_slider, on_analog_value_changed, LV_EVENT_VALUE_CHANGED, nullptr);
    lv_obj_add_event_cb(s_analog_slider, on_analog_released, LV_EVENT_RELEASED, nullptr);

    // Bottom actions.
    lv_obj_t *btn_row = lv_obj_create(body);
    lv_obj_remove_style_all(btn_row);
    lv_obj_set_width(btn_row, LV_PCT(100));
    lv_obj_set_height(btn_row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(btn_row, 16, 0);
    lv_obj_clear_flag(btn_row, LV_OBJ_FLAG_SCROLLABLE);

    s_all_off_btn = make_small_btn(btn_row, "All Outputs Off", on_all_off_clicked);
    s_uart_test_btn = make_small_btn(btn_row, "UART Test", on_uart_test_clicked);

    lv_timer_create(poll, 1000, nullptr);
}

void ui_fn_output_show()
{
    s_visible = true;
    apply_model_to_ui();
    refresh();
    lv_obj_clear_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
}
