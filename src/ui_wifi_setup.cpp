#include "ui_wifi_setup.h"

#include <WiFi.h>

#include "app_state.h"
#include "config.h"

namespace
{
    // Same board-specific fragility as everywhere else in this app: plain
    // widgets (not lv_list) in a pre-allocated pool, only ever mutated via
    // text/visibility after boot - see ui_home.cpp's kMaxDriveRows comment.
    constexpr int kMaxScanResults = 12;

    lv_obj_t *s_overlay = nullptr;

    lv_obj_t *s_list_page = nullptr;
    lv_obj_t *s_status_label = nullptr;
    lv_obj_t *s_scan_btns[kMaxScanResults];
    lv_obj_t *s_scan_lbls[kMaxScanResults];
    String s_scan_ssids[kMaxScanResults];

    lv_obj_t *s_password_page = nullptr;
    lv_obj_t *s_ssid_label = nullptr;
    lv_obj_t *s_password_ta = nullptr;
    lv_obj_t *s_keyboard = nullptr;
    String s_pending_ssid;
    lv_color_t s_default_btn_bg;
    int s_shown_rows = 0;

    lv_obj_t *s_wifi_toggle_btn = nullptr;
    lv_obj_t *s_wifi_toggle_lbl = nullptr;

    int s_wifi_connect_attempts = 0;

    void show_list_page()
    {
        lv_obj_add_flag(s_password_page, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_list_page, LV_OBJ_FLAG_HIDDEN);
    }

    // `password` pre-fills the field with a previously-saved password (if
    // any) so re-joining a known network is still just "tap network, tap
    // Connect" - but always landing here (rather than silently connecting
    // for known networks) means a bad/incomplete saved password can always
    // be fixed by just editing what's already there before confirming.
    void show_password_page(const String &ssid, const String &password)
    {
        s_pending_ssid = ssid;
        lv_label_set_text(s_ssid_label, ssid.c_str());
        lv_textarea_set_text(s_password_ta, password.c_str());
        lv_obj_add_flag(s_list_page, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_password_page, LV_OBJ_FLAG_HIDDEN);
        lv_keyboard_set_textarea(s_keyboard, s_password_ta);
        lv_obj_clear_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
    }

    void refresh_row_indicators();

    // Same "join, then actually verify" pattern as the rest of the app:
    // WiFi.begin() is async, so poll status() rather than assuming success.
    // On failure, drop into the password page (pre-filled with whatever was
    // just tried) instead of just an error on the list page, whether this
    // attempt came from a saved network's direct-connect or a manually
    // typed password - either way the user needs a chance to fix it.
    void poll_wifi_connect(lv_timer_t *timer)
    {
        if (WiFi.status() == WL_CONNECTED)
        {
            lv_label_set_text_fmt(s_status_label, "Connected to %s: %s",
                                   WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
            refresh_row_indicators();
            lv_timer_delete(timer);
            return;
        }
        if (++s_wifi_connect_attempts >= 20) // ~10s at 500ms
        {
            lv_timer_delete(timer);
            String ssid = s_pending_ssid;
            String triedPassword = g_config.wifi_password;
            show_password_page(ssid, triedPassword);
            lv_label_set_text_fmt(s_ssid_label, "%s - failed, check password", ssid.c_str());
        }
    }

    void connect_and_remember(const String &ssid, const String &password)
    {
        s_pending_ssid = ssid;
        g_config.wifi_ssid = ssid;
        g_config.wifi_password = password;
        g_config.rememberNetwork(ssid, password);
        config_save(g_config);
        app_wifi_apply();

        lv_label_set_text(s_status_label, "Connecting...");
        show_list_page();

        s_wifi_connect_attempts = 0;
        lv_timer_create(poll_wifi_connect, 500, nullptr);
    }

    // Updates each visible row's checkmark and saved-password color against
    // the *current* connection state, without needing a fresh scan - reused
    // right after a connect attempt succeeds, since the row list from the
    // last scan is still valid but its indicators would otherwise go stale
    // (nothing re-scans just because the connection changed).
    void refresh_row_indicators()
    {
        String connectedSsid = (WiFi.status() == WL_CONNECTED) ? WiFi.SSID() : String();

        for (int i = 0; i < s_shown_rows; i++)
        {
            const String &ssid = s_scan_ssids[i];
            String display = ssid.length() ? ssid : "(hidden network)";
            String text = (ssid.length() && ssid == connectedSsid) ? (LV_SYMBOL_OK " " + display) : display;
            lv_label_set_text(s_scan_lbls[i], text.c_str());

            String unused;
            bool saved = ssid.length() && g_config.findSavedPassword(ssid, unused);
            lv_obj_set_style_bg_color(s_scan_btns[i],
                                       saved ? lv_palette_main(LV_PALETTE_GREEN) : s_default_btn_bg, 0);
        }
    }

    // Only mutates the pre-existing pool (text/visibility/color) - never
    // creates or deletes widgets. See kMaxScanResults comment above.
    void populate_scan_results(int count)
    {
        String connectedSsid = (WiFi.status() == WL_CONNECTED) ? WiFi.SSID() : String();

        // De-duplicate by SSID - routers/mesh systems commonly broadcast the
        // same network name from multiple APs or channels, which raw scan
        // results list as separate entries. Which duplicate we keep doesn't
        // matter for connecting: WiFi.begin() picks among all matching APs
        // for that SSID on its own regardless of which scan index we show.
        s_shown_rows = 0;
        for (int i = 0; i < count && s_shown_rows < kMaxScanResults; i++)
        {
            String ssid = WiFi.SSID(i);

            bool alreadyShown = false;
            for (int j = 0; j < s_shown_rows; j++)
            {
                if (s_scan_ssids[j] == ssid)
                {
                    alreadyShown = true;
                    break;
                }
            }
            if (alreadyShown)
                continue;

            s_scan_ssids[s_shown_rows] = ssid;
            s_shown_rows++;
        }

        if (connectedSsid.length())
            lv_label_set_text_fmt(s_status_label, "Connected to %s: %s",
                                   connectedSsid.c_str(), WiFi.localIP().toString().c_str());
        else
            lv_label_set_text(s_status_label, s_shown_rows > 0 ? "Tap a network to join" : "No networks found");

        for (int i = 0; i < s_shown_rows; i++)
            lv_obj_clear_flag(s_scan_btns[i], LV_OBJ_FLAG_HIDDEN);
        for (int i = s_shown_rows; i < kMaxScanResults; i++)
            lv_obj_add_flag(s_scan_btns[i], LV_OBJ_FLAG_HIDDEN);

        refresh_row_indicators();

        WiFi.scanDelete();
    }

    // WiFi.scanNetworks() (blocking form) commonly takes several seconds,
    // which would freeze the whole UI (touch included) for that long. Poll
    // the async form from a timer instead so the main loop keeps running.
    void poll_scan_results(lv_timer_t *timer)
    {
        int count = WiFi.scanComplete();
        if (count == WIFI_SCAN_RUNNING)
            return;
        if (count == WIFI_SCAN_FAILED)
            count = 0;

        populate_scan_results(count);
        lv_timer_delete(timer);
    }

    void start_scan()
    {
        lv_label_set_text(s_status_label, "Scanning...");
        WiFi.mode(WIFI_STA);
        WiFi.scanNetworks(/*async=*/true);
        lv_timer_create(poll_scan_results, 250, nullptr);
    }

    void on_scan_clicked(lv_event_t *)
    {
        start_scan();
    }

    void on_row_clicked(lv_event_t *e)
    {
        int idx = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
        const String &ssid = s_scan_ssids[idx];

        String savedPassword;
        if (g_config.findSavedPassword(ssid, savedPassword))
            connect_and_remember(ssid, savedPassword); // falls back to the password page on failure
        else
            show_password_page(ssid, "");
    }

    void on_connect_clicked(lv_event_t *)
    {
        connect_and_remember(s_pending_ssid, lv_textarea_get_text(s_password_ta));
    }

    void on_cancel_password_clicked(lv_event_t *)
    {
        lv_obj_add_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
        lv_keyboard_set_textarea(s_keyboard, nullptr);
        show_list_page();
    }

    void on_toggle_password(lv_event_t *e)
    {
        lv_obj_t *cb = static_cast<lv_obj_t *>(lv_event_get_target(e));
        lv_textarea_set_password_mode(s_password_ta, !lv_obj_has_state(cb, LV_STATE_CHECKED));
    }

    void on_back_clicked(lv_event_t *)
    {
        lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
    }

    // Reflects g_config.wifi_enabled (the persisted on/off intent), not live
    // WiFi.status() - while enabled but still mid-connect (or the saved AP
    // is out of range), this should keep reading "Disable", not flicker to
    // "Enable" just because the radio isn't associated yet.
    void refresh_wifi_toggle_label()
    {
        lv_label_set_text(s_wifi_toggle_lbl, g_config.wifi_enabled ? "Disable" : "Enable");
    }

    // Same toggle the top bar's Wi-Fi icon performs (see ui_shell.cpp's
    // on_wifi_icon_clicked for why both the radio-mode dance and the
    // persisted flag matter), just reachable from this page too.
    void on_wifi_toggle_clicked(lv_event_t *)
    {
        if (g_config.wifi_enabled)
        {
            app_wifi_disable();
            lv_label_set_text(s_status_label, "Wi-Fi disabled.");
        }
        else
        {
            g_config.wifi_enabled = true;
            config_save(g_config);
            app_wifi_apply();
            lv_label_set_text(s_status_label, "Reconnecting...");
        }
        refresh_wifi_toggle_label();
    }
}

void ui_wifi_setup_build_pool()
{
    s_overlay = lv_obj_create(lv_layer_top());
    lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_size(s_overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_overlay, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(s_overlay, 16, 0);
    lv_obj_clear_flag(s_overlay, LV_OBJ_FLAG_SCROLLABLE);

    // --- list page ---
    s_list_page = lv_obj_create(s_overlay);
    lv_obj_remove_style_all(s_list_page);
    lv_obj_set_size(s_list_page, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(s_list_page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_list_page, 8, 0);
    lv_obj_clear_flag(s_list_page, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(s_list_page);
    lv_label_set_text(title, "Wi-Fi");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);

    s_status_label = lv_label_create(s_list_page);
    lv_label_set_text(s_status_label, "");
    lv_obj_set_style_text_color(s_status_label, lv_color_white(), 0);
    lv_obj_set_width(s_status_label, LV_PCT(100));

    lv_obj_t *rows_wrap = lv_obj_create(s_list_page);
    lv_obj_remove_style_all(rows_wrap);
    lv_obj_add_flag(rows_wrap, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(rows_wrap, LV_DIR_VER);
    lv_obj_set_flex_flow(rows_wrap, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(rows_wrap, 6, 0);
    lv_obj_set_width(rows_wrap, LV_PCT(100));
    lv_obj_set_flex_grow(rows_wrap, 1);

    for (int i = 0; i < kMaxScanResults; i++)
    {
        lv_obj_t *btn = lv_button_create(rows_wrap);
        lv_obj_set_width(btn, LV_PCT(100));
        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, "");
        lv_obj_add_flag(btn, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_event_cb(btn, on_row_clicked, LV_EVENT_CLICKED,
                             reinterpret_cast<void *>(static_cast<intptr_t>(i)));
        s_scan_btns[i] = btn;
        s_scan_lbls[i] = lbl;
        if (i == 0)
            s_default_btn_bg = lv_obj_get_style_bg_color(btn, LV_PART_MAIN);
    }

    lv_obj_t *bottom_row = lv_obj_create(s_list_page);
    lv_obj_remove_style_all(bottom_row);
    lv_obj_set_width(bottom_row, LV_PCT(100));
    lv_obj_set_height(bottom_row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(bottom_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(bottom_row, 8, 0);

    lv_obj_t *scan_btn = lv_button_create(bottom_row);
    lv_obj_set_flex_grow(scan_btn, 1);
    lv_obj_set_height(scan_btn, 70);
    lv_obj_t *scan_lbl = lv_label_create(scan_btn);
    lv_label_set_text(scan_lbl, LV_SYMBOL_REFRESH " Scan");
    lv_obj_set_style_text_font(scan_lbl, &lv_font_montserrat_24, 0);
    lv_obj_center(scan_lbl);
    lv_obj_add_event_cb(scan_btn, on_scan_clicked, LV_EVENT_CLICKED, nullptr);

    s_wifi_toggle_btn = lv_button_create(bottom_row);
    lv_obj_set_flex_grow(s_wifi_toggle_btn, 1);
    lv_obj_set_height(s_wifi_toggle_btn, 70);
    s_wifi_toggle_lbl = lv_label_create(s_wifi_toggle_btn);
    lv_obj_set_style_text_font(s_wifi_toggle_lbl, &lv_font_montserrat_24, 0);
    lv_obj_center(s_wifi_toggle_lbl);
    lv_obj_add_event_cb(s_wifi_toggle_btn, on_wifi_toggle_clicked, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *back_btn = lv_button_create(bottom_row);
    lv_obj_set_flex_grow(back_btn, 1);
    lv_obj_set_height(back_btn, 70);
    lv_obj_t *back_lbl = lv_label_create(back_btn);
    lv_label_set_text(back_lbl, "Back");
    lv_obj_set_style_text_font(back_lbl, &lv_font_montserrat_24, 0);
    lv_obj_center(back_lbl);
    lv_obj_add_event_cb(back_btn, on_back_clicked, LV_EVENT_CLICKED, nullptr);

    // --- password page: SSID + password field + keyboard only, nothing
    // else, per the "larger print, easier buttons" request ---
    s_password_page = lv_obj_create(s_overlay);
    lv_obj_remove_style_all(s_password_page);
    lv_obj_add_flag(s_password_page, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_size(s_password_page, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(s_password_page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_password_page, 8, 0);
    lv_obj_clear_flag(s_password_page, LV_OBJ_FLAG_SCROLLABLE);

    s_ssid_label = lv_label_create(s_password_page);
    lv_label_set_text(s_ssid_label, "");
    lv_obj_set_style_text_color(s_ssid_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(s_ssid_label, &lv_font_montserrat_28, 0);

    // Password field + "Show" checkbox side by side, checkbox at the end of
    // the row (not below, where a mis-tap could land on Connect/Cancel
    // instead and submit an incomplete password).
    lv_obj_t *pw_row = lv_obj_create(s_password_page);
    lv_obj_remove_style_all(pw_row);
    lv_obj_set_width(pw_row, LV_PCT(100));
    lv_obj_set_height(pw_row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(pw_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(pw_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(pw_row, 8, 0);

    s_password_ta = lv_textarea_create(pw_row);
    lv_obj_set_flex_grow(s_password_ta, 1);
    lv_textarea_set_one_line(s_password_ta, true);
    lv_textarea_set_password_mode(s_password_ta, true);
    lv_textarea_set_placeholder_text(s_password_ta, "Password");
    lv_obj_set_style_text_font(s_password_ta, &lv_font_montserrat_24, 0);

    lv_obj_t *show_cb = lv_checkbox_create(pw_row);
    lv_checkbox_set_text(show_cb, "Show");
    lv_obj_set_style_text_font(show_cb, &lv_font_montserrat_24, 0);
    // Double the default ~20px indicator box so it's easier to hit.
    lv_obj_set_style_width(show_cb, 40, LV_PART_INDICATOR);
    lv_obj_set_style_height(show_cb, 40, LV_PART_INDICATOR);
    lv_obj_add_event_cb(show_cb, on_toggle_password, LV_EVENT_VALUE_CHANGED, nullptr);

    lv_obj_t *pw_btn_row = lv_obj_create(s_password_page);
    lv_obj_remove_style_all(pw_btn_row);
    lv_obj_set_width(pw_btn_row, LV_PCT(100));
    lv_obj_set_height(pw_btn_row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(pw_btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(pw_btn_row, 8, 0);

    lv_obj_t *connect_btn = lv_button_create(pw_btn_row);
    lv_obj_set_flex_grow(connect_btn, 1);
    lv_obj_set_height(connect_btn, 70);
    lv_obj_t *connect_lbl = lv_label_create(connect_btn);
    lv_label_set_text(connect_lbl, "Connect");
    lv_obj_set_style_text_font(connect_lbl, &lv_font_montserrat_24, 0);
    lv_obj_center(connect_lbl);
    lv_obj_add_event_cb(connect_btn, on_connect_clicked, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *cancel_btn = lv_button_create(pw_btn_row);
    lv_obj_set_flex_grow(cancel_btn, 1);
    lv_obj_set_height(cancel_btn, 70);
    lv_obj_t *cancel_lbl = lv_label_create(cancel_btn);
    lv_label_set_text(cancel_lbl, "Cancel");
    lv_obj_set_style_text_font(cancel_lbl, &lv_font_montserrat_24, 0);
    lv_obj_center(cancel_lbl);
    lv_obj_add_event_cb(cancel_btn, on_cancel_password_clicked, LV_EVENT_CLICKED, nullptr);

    s_keyboard = lv_keyboard_create(s_password_page);
    lv_obj_add_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_flex_grow(s_keyboard, 1);
}

void ui_wifi_setup_show()
{
    show_list_page();
    lv_obj_clear_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
    refresh_wifi_toggle_label();
    start_scan();
}
