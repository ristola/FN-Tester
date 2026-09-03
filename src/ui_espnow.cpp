#include "ui_espnow.h"

#include <WiFi.h>

#include "app_state.h"
#include "espnow_state.h"

namespace
{
    // ---- Main ESP NOW page ----

    lv_obj_t *s_overlay = nullptr;
    lv_obj_t *s_device_id_label = nullptr;
    lv_obj_t *s_mac_label = nullptr;
    lv_obj_t *s_channel_label = nullptr;
    lv_obj_t *s_status_label = nullptr;
    lv_obj_t *s_last_ping_label = nullptr;

    bool s_page_visible = false;

    // ---- Setup sub-page (Name field) ----

    lv_obj_t *s_setup_overlay = nullptr;
    lv_obj_t *s_name_ta = nullptr;

    lv_obj_t *s_name_entry_overlay = nullptr;
    lv_obj_t *s_name_entry_ta = nullptr;
    lv_obj_t *s_name_entry_keyboard = nullptr;

    // ---- Monitor sub-page (traffic log) ----

    lv_obj_t *s_monitor_overlay = nullptr;
    lv_obj_t *s_monitor_empty_label = nullptr;
    lv_obj_t *s_monitor_rows[kMaxTrafficLogEntries];
    bool s_monitor_visible = false;

    // ---- Diagnostics sub-page (paired bridges + remote actions) ----

    lv_obj_t *s_diag_overlay = nullptr;
    lv_obj_t *s_diag_empty_label = nullptr;
    lv_obj_t *s_diag_cards[kMaxPairedBridges];
    lv_obj_t *s_diag_labels[kMaxPairedBridges];
    bool s_diag_visible = false;

    lv_obj_t *make_field_label(lv_obj_t *parent)
    {
        lv_obj_t *lbl = lv_label_create(parent);
        lv_label_set_text(lbl, "");
        lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_24, 0);
        lv_obj_set_width(lbl, LV_PCT(100));
        return lbl;
    }

    // Manual alignment, not a flex row with space-between - same reasoning
    // as every other page header in this app: the title needs to sit
    // dead-center regardless of the (left-aligned) Back button's width.
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

    void on_back_clicked(lv_event_t *)
    {
        s_page_visible = false;
        lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
    }

    void refresh_last_ping()
    {
        const SM_LastPing *ping = espnow_last_ping();
        if (ping == nullptr)
        {
            lv_label_set_text(s_last_ping_label, "No ping received yet");
            return;
        }
        uint32_t ageSec = (millis() - ping->receivedMs) / 1000;
        lv_label_set_text_fmt(s_last_ping_label, "Ping received from %s - %us ago", ping->friendlyName, ageSec);
    }

    void poll_refresh(lv_timer_t *)
    {
        if (!s_page_visible)
            return;
        refresh_last_ping();
    }

    // ---- Setup sub-page ----

    void on_setup_back_clicked(lv_event_t *)
    {
        lv_obj_add_flag(s_setup_overlay, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
    }

    void on_setup_clicked(lv_event_t *)
    {
        lv_textarea_set_text(s_name_ta, g_config.espnow_friendly_name.c_str());
        lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_setup_overlay, LV_OBJ_FLAG_HIDDEN);
    }

    // Same dedicated full-page keyboard entry pattern used elsewhere in this
    // app (e.g. the Wi-Fi password page): tapping the on-page field opens
    // this instead of a small docked keyboard; Cancel discards, the
    // keyboard's Enter key saves.
    void on_name_focused(lv_event_t *)
    {
        lv_textarea_set_text(s_name_entry_ta, g_config.espnow_friendly_name.c_str());
        lv_obj_add_flag(s_setup_overlay, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_name_entry_overlay, LV_OBJ_FLAG_HIDDEN);
    }

    void on_name_entry_cancel_clicked(lv_event_t *)
    {
        lv_obj_add_flag(s_name_entry_overlay, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_setup_overlay, LV_OBJ_FLAG_HIDDEN);
    }

    // Listens on the textarea, not the keyboard - the return-arrow key only
    // fires LV_EVENT_READY on the bound textarea, not the keyboard object.
    void on_name_entry_ready(lv_event_t *)
    {
        String name = lv_textarea_get_text(s_name_entry_ta);
        name.trim();
        if (name.length() == 0)
            name = "Ultimate - 01";
        // SM_DeviceIdentity.friendlyName is a fixed char[24] (23 chars+NUL).
        if (name.length() > 23)
            name = name.substring(0, 23);

        g_config.espnow_friendly_name = name;
        config_save(g_config);
        lv_textarea_set_text(s_name_ta, name.c_str());

        lv_obj_add_flag(s_name_entry_overlay, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_setup_overlay, LV_OBJ_FLAG_HIDDEN);
    }

    // ---- Monitor sub-page ----

    void on_monitor_back_clicked(lv_event_t *)
    {
        s_monitor_visible = false;
        lv_obj_add_flag(s_monitor_overlay, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
    }

    void refresh_monitor()
    {
        int count = espnow_traffic_log_count();

        if (count == 0)
            lv_obj_clear_flag(s_monitor_empty_label, LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_add_flag(s_monitor_empty_label, LV_OBJ_FLAG_HIDDEN);

        for (int i = 0; i < kMaxTrafficLogEntries; i++)
        {
            if (i >= count)
            {
                lv_obj_add_flag(s_monitor_rows[i], LV_OBJ_FLAG_HIDDEN);
                continue;
            }

            const SM_TrafficLogEntry *entry = espnow_traffic_log_entry(i);
            uint32_t ageSec = (millis() - entry->timestampMs) / 1000;
            lv_label_set_text_fmt(s_monitor_rows[i], "%s  %-12s  %s - %us ago",
                                   entry->outgoing ? "OUT ->" : "IN  <-",
                                   espnow_message_type_name(entry->messageType),
                                   entry->peerLabel, ageSec);
            lv_obj_set_style_text_color(s_monitor_rows[i],
                                         entry->outgoing ? lv_palette_lighten(LV_PALETTE_GREEN, 2)
                                                          : lv_palette_lighten(LV_PALETTE_CYAN, 2),
                                         0);
            lv_obj_clear_flag(s_monitor_rows[i], LV_OBJ_FLAG_HIDDEN);
        }
    }

    void poll_monitor(lv_timer_t *)
    {
        if (!s_monitor_visible)
            return;
        refresh_monitor();
    }

    void on_send_test_ping_clicked(lv_event_t *)
    {
        espnow_send_test_ping();
        refresh_monitor(); // reflect the outgoing entry immediately, don't wait for the next poll tick
    }

    void on_monitor_clicked(lv_event_t *)
    {
        s_monitor_visible = true;
        refresh_monitor();
        lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_monitor_overlay, LV_OBJ_FLAG_HIDDEN);
    }

    // ---- Diagnostics sub-page ----

    void on_diag_back_clicked(lv_event_t *)
    {
        s_diag_visible = false;
        lv_obj_add_flag(s_diag_overlay, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
    }

    void refresh_diagnostics()
    {
        int count = espnow_paired_bridge_count();

        if (count == 0)
            lv_obj_clear_flag(s_diag_empty_label, LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_add_flag(s_diag_empty_label, LV_OBJ_FLAG_HIDDEN);

        for (int i = 0; i < kMaxPairedBridges; i++)
        {
            if (i >= count)
            {
                lv_obj_add_flag(s_diag_cards[i], LV_OBJ_FLAG_HIDDEN);
                continue;
            }

            const SM_PairedBridge *bridge = espnow_paired_bridge(i);
            lv_label_set_text_fmt(s_diag_labels[i], "%s (%s)\n%02X:%02X:%02X:%02X:%02X:%02X",
                                   bridge->friendlyName, espnow_device_type_name(bridge->deviceType),
                                   bridge->mac[0], bridge->mac[1], bridge->mac[2],
                                   bridge->mac[3], bridge->mac[4], bridge->mac[5]);
            lv_obj_clear_flag(s_diag_cards[i], LV_OBJ_FLAG_HIDDEN);
        }
    }

    void poll_diagnostics(lv_timer_t *)
    {
        if (!s_diag_visible)
            return;
        refresh_diagnostics();
    }

    void on_diag_clicked(lv_event_t *)
    {
        s_diag_visible = true;
        refresh_diagnostics();
        lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_diag_overlay, LV_OBJ_FLAG_HIDDEN);
    }

    void on_diag_reboot_clicked(lv_event_t *e)
    {
        int index = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
        espnow_send_command(index, "REBOOT");
    }

    void on_diag_restore_clicked(lv_event_t *e)
    {
        int index = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
        espnow_send_command(index, "FACTORY_RESET");
    }

    void on_diag_forget_clicked(lv_event_t *e)
    {
        int index = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
        espnow_forget_paired_bridge(index);
        refresh_diagnostics(); // don't wait for the next poll tick - reflect the removal immediately
    }

    lv_obj_t *make_diag_action_btn(lv_obj_t *parent, const char *text, lv_event_cb_t cb, int index, bool disabled)
    {
        lv_obj_t *btn = lv_button_create(parent);
        lv_obj_set_size(btn, 130, 56);
        if (disabled)
            lv_obj_add_state(btn, LV_STATE_DISABLED);
        else
            lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, reinterpret_cast<void *>(static_cast<intptr_t>(index)));
        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, text);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_18, 0);
        lv_obj_center(lbl);
        return btn;
    }
}

void ui_espnow_build_pool()
{
    // ---- Main page ----

    s_overlay = make_full_screen_overlay();
    make_header(s_overlay, "ESP NOW", on_back_clicked);

    s_device_id_label = make_field_label(s_overlay);
    s_mac_label = make_field_label(s_overlay);
    s_channel_label = make_field_label(s_overlay);

    s_status_label = lv_label_create(s_overlay);
    lv_label_set_text(s_status_label,
                       "Broadcasting a heartbeat every 10s and listening for other devices. "
                       "An unpaired FN bridge sweeping channels is offered pairing automatically.");
    lv_obj_set_style_text_color(s_status_label, lv_palette_main(LV_PALETTE_GREY), 0);
    lv_obj_set_style_text_font(s_status_label, &lv_font_montserrat_18, 0);
    lv_obj_set_width(s_status_label, LV_PCT(100));

    s_last_ping_label = lv_label_create(s_overlay);
    lv_label_set_text(s_last_ping_label, "No ping received yet");
    lv_obj_set_style_text_color(s_last_ping_label, lv_palette_main(LV_PALETTE_GREY), 0);
    lv_obj_set_style_text_font(s_last_ping_label, &lv_font_montserrat_18, 0);
    lv_obj_set_width(s_last_ping_label, LV_PCT(100));

    // Absorbs the remaining vertical space in the overlay's flex column,
    // pushing the nav row down to sit at the bottom of the screen instead
    // of directly under the last-ping label.
    lv_obj_t *spacer = lv_obj_create(s_overlay);
    lv_obj_remove_style_all(spacer);
    lv_obj_set_width(spacer, LV_PCT(100));
    lv_obj_set_flex_grow(spacer, 1);
    lv_obj_clear_flag(spacer, LV_OBJ_FLAG_SCROLLABLE);

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
        {"Setup", on_setup_clicked},
        {"Monitor", on_monitor_clicked},
        {"Diagnostics", on_diag_clicked},
    };
    for (const auto &def : navButtons)
    {
        lv_obj_t *btn = lv_button_create(nav_row);
        lv_obj_set_size(btn, 180, 60);
        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, def.text);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_24, 0);
        lv_obj_center(lbl);
        lv_obj_add_event_cb(btn, def.cb, LV_EVENT_CLICKED, nullptr);
    }

    lv_timer_create(poll_refresh, 2000, nullptr);

    // ---- Setup sub-page ----

    s_setup_overlay = make_full_screen_overlay();
    make_header(s_setup_overlay, "Setup", on_setup_back_clicked);

    lv_obj_t *name_row = lv_obj_create(s_setup_overlay);
    lv_obj_remove_style_all(name_row);
    lv_obj_set_width(name_row, LV_PCT(100));
    lv_obj_set_height(name_row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(name_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(name_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(name_row, 10, 0);

    lv_obj_t *name_lbl = lv_label_create(name_row);
    lv_label_set_text(name_lbl, "Name:");
    lv_obj_set_style_text_color(name_lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(name_lbl, &lv_font_montserrat_24, 0);

    s_name_ta = lv_textarea_create(name_row);
    lv_obj_set_flex_grow(s_name_ta, 1);
    lv_textarea_set_one_line(s_name_ta, true);
    lv_obj_set_style_text_font(s_name_ta, &lv_font_montserrat_24, 0);
    lv_obj_add_event_cb(s_name_ta, on_name_focused, LV_EVENT_FOCUSED, nullptr);

    // Dedicated full-page keyboard entry screen for the Name field.
    s_name_entry_overlay = lv_obj_create(lv_layer_top());
    lv_obj_add_flag(s_name_entry_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_size(s_name_entry_overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_name_entry_overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_name_entry_overlay, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(s_name_entry_overlay, 16, 0);
    lv_obj_set_flex_flow(s_name_entry_overlay, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_name_entry_overlay, 12, 0);
    lv_obj_clear_flag(s_name_entry_overlay, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *name_entry_header = lv_obj_create(s_name_entry_overlay);
    lv_obj_remove_style_all(name_entry_header);
    lv_obj_set_width(name_entry_header, LV_PCT(100));
    lv_obj_set_height(name_entry_header, 56);
    lv_obj_clear_flag(name_entry_header, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *name_cancel_btn = lv_button_create(name_entry_header);
    lv_obj_set_size(name_cancel_btn, 120, 56);
    lv_obj_align(name_cancel_btn, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_t *name_cancel_lbl = lv_label_create(name_cancel_btn);
    lv_label_set_text(name_cancel_lbl, "Cancel");
    lv_obj_set_style_text_font(name_cancel_lbl, &lv_font_montserrat_24, 0);
    lv_obj_center(name_cancel_lbl);
    lv_obj_add_event_cb(name_cancel_btn, on_name_entry_cancel_clicked, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *name_entry_title = lv_label_create(name_entry_header);
    lv_label_set_text(name_entry_title, "Enter Name");
    lv_obj_set_style_text_color(name_entry_title, lv_color_white(), 0);
    lv_obj_set_style_text_font(name_entry_title, &lv_font_montserrat_28, 0);
    lv_obj_align(name_entry_title, LV_ALIGN_CENTER, 0, 0);

    s_name_entry_ta = lv_textarea_create(s_name_entry_overlay);
    lv_obj_set_width(s_name_entry_ta, LV_PCT(100));
    lv_textarea_set_one_line(s_name_entry_ta, true);
    lv_obj_set_style_text_font(s_name_entry_ta, &lv_font_montserrat_28, 0);

    // Alphanumeric (letters + a numbers row via the keyboard's own "123"
    // toggle key) - the friendly name can mix both.
    s_name_entry_keyboard = lv_keyboard_create(s_name_entry_overlay);
    lv_keyboard_set_mode(s_name_entry_keyboard, LV_KEYBOARD_MODE_TEXT_UPPER);
    lv_keyboard_set_textarea(s_name_entry_keyboard, s_name_entry_ta);
    lv_obj_set_flex_grow(s_name_entry_keyboard, 1);
    lv_obj_add_event_cb(s_name_entry_ta, on_name_entry_ready, LV_EVENT_READY, nullptr);

    // ---- Monitor sub-page ----

    s_monitor_overlay = make_full_screen_overlay();
    make_header(s_monitor_overlay, "Monitor", on_monitor_back_clicked);

    lv_obj_t *send_ping_btn = lv_button_create(s_monitor_overlay);
    lv_obj_set_size(send_ping_btn, 220, 56);
    lv_obj_t *send_ping_lbl = lv_label_create(send_ping_btn);
    lv_label_set_text(send_ping_lbl, "Send Test Ping");
    lv_obj_set_style_text_font(send_ping_lbl, &lv_font_montserrat_24, 0);
    lv_obj_center(send_ping_lbl);
    lv_obj_add_event_cb(send_ping_btn, on_send_test_ping_clicked, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *monitor_wrap = lv_obj_create(s_monitor_overlay);
    lv_obj_remove_style_all(monitor_wrap);
    lv_obj_add_flag(monitor_wrap, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(monitor_wrap, LV_DIR_VER);
    lv_obj_set_flex_flow(monitor_wrap, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(monitor_wrap, 4, 0);
    lv_obj_set_width(monitor_wrap, LV_PCT(100));
    lv_obj_set_flex_grow(monitor_wrap, 1);

    s_monitor_empty_label = lv_label_create(monitor_wrap);
    lv_label_set_text(s_monitor_empty_label, "No ESP-NOW traffic logged yet");
    lv_obj_set_style_text_color(s_monitor_empty_label, lv_palette_main(LV_PALETTE_GREY), 0);
    lv_obj_set_style_text_font(s_monitor_empty_label, &lv_font_montserrat_18, 0);

    for (int i = 0; i < kMaxTrafficLogEntries; i++)
    {
        lv_obj_t *row = lv_label_create(monitor_wrap);
        lv_label_set_text(row, "");
        lv_obj_set_style_text_font(row, &lv_font_montserrat_18, 0);
        lv_obj_set_width(row, LV_PCT(100));
        lv_obj_add_flag(row, LV_OBJ_FLAG_HIDDEN);
        s_monitor_rows[i] = row;
    }

    lv_timer_create(poll_monitor, 1000, nullptr);

    // ---- Diagnostics sub-page ----

    s_diag_overlay = make_full_screen_overlay();
    make_header(s_diag_overlay, "Diagnostics", on_diag_back_clicked);

    lv_obj_t *diag_wrap = lv_obj_create(s_diag_overlay);
    lv_obj_remove_style_all(diag_wrap);
    lv_obj_add_flag(diag_wrap, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(diag_wrap, LV_DIR_VER);
    lv_obj_set_flex_flow(diag_wrap, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(diag_wrap, 10, 0);
    lv_obj_set_width(diag_wrap, LV_PCT(100));
    lv_obj_set_flex_grow(diag_wrap, 1);

    s_diag_empty_label = lv_label_create(diag_wrap);
    lv_label_set_text(s_diag_empty_label, "No bridges paired yet");
    lv_obj_set_style_text_color(s_diag_empty_label, lv_palette_main(LV_PALETTE_GREY), 0);
    lv_obj_set_style_text_font(s_diag_empty_label, &lv_font_montserrat_18, 0);

    for (int i = 0; i < kMaxPairedBridges; i++)
    {
        lv_obj_t *card = lv_obj_create(diag_wrap);
        // Plain lv_obj_create() keeps the default theme's light panel
        // background - without stripping it, the white label text below
        // renders white-on-white (invisible) instead of white-on-black
        // like every other list row in this app.
        lv_obj_remove_style_all(card);
        lv_obj_set_width(card, LV_PCT(100));
        lv_obj_set_height(card, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(card, 8, 0);
        lv_obj_set_style_pad_all(card, 10, 0);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(card, LV_OBJ_FLAG_HIDDEN);

        lv_obj_t *lbl = lv_label_create(card);
        lv_label_set_text(lbl, "");
        lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_18, 0);
        lv_obj_set_width(lbl, LV_PCT(100));

        lv_obj_t *actions_row = lv_obj_create(card);
        lv_obj_remove_style_all(actions_row);
        lv_obj_set_width(actions_row, LV_PCT(100));
        lv_obj_set_height(actions_row, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(actions_row, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_column(actions_row, 10, 0);
        lv_obj_clear_flag(actions_row, LV_OBJ_FLAG_SCROLLABLE);

        make_diag_action_btn(actions_row, "Reboot", on_diag_reboot_clicked, i, false);
        make_diag_action_btn(actions_row, "Restore", on_diag_restore_clicked, i, false);
        make_diag_action_btn(actions_row, "Forget", on_diag_forget_clicked, i, false);
        make_diag_action_btn(actions_row, "Update (soon)", nullptr, i, true);

        s_diag_cards[i] = card;
        s_diag_labels[i] = lbl;
    }

    lv_timer_create(poll_diagnostics, 1000, nullptr);
}

void ui_espnow_show()
{
    s_page_visible = true;
    lv_obj_add_flag(s_setup_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_name_entry_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_monitor_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_diag_overlay, LV_OBJ_FLAG_HIDDEN);
    s_monitor_visible = false;
    s_diag_visible = false;

    lv_label_set_text_fmt(s_device_id_label, "Device ID: 0x%08X", espnow_local_device_id());
    lv_label_set_text_fmt(s_mac_label, "MAC Address: %s", WiFi.macAddress().c_str());

    // ESP-NOW works on a fixed channel whether or not Wi-Fi is connected -
    // see app_wifi_service()'s fallback-channel logic - so this always
    // shows a real channel number now, just flagged when it's the no-AP
    // fallback rather than a real Wi-Fi association.
    if (WiFi.status() == WL_CONNECTED)
        lv_label_set_text_fmt(s_channel_label, "Channel: %d", WiFi.channel());
    else
        lv_label_set_text_fmt(s_channel_label, "Channel: %d (no Wi-Fi - fallback)", WiFi.channel());

    refresh_last_ping();

    lv_obj_clear_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
}
