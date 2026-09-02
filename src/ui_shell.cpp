#include "ui_shell.h"

#include <WiFi.h>

#include "app_state.h"
#include "espnow_state.h"
#include "ui_espnow.h"
#include "ui_setup.h"
#include "ui_wifi_setup.h"

namespace
{
    constexpr const char *kAppTitle = "FN Tester";

    lv_obj_t *s_drawer = nullptr;
    lv_obj_t *s_wifi_bar1 = nullptr;
    lv_obj_t *s_wifi_bar2 = nullptr;
    lv_obj_t *s_wifi_bar3 = nullptr;
    lv_obj_t *s_wifi_bar4 = nullptr;
    lv_obj_t *s_wifi_badge = nullptr;
    lv_obj_t *s_title_label = nullptr;
    bool s_wifi_was_connected = false;

    // Modal "pair this FN bridge?" dialog - visible regardless of which tab
    // or overlay is currently showing, since an incoming SM_PROVISION_REQUEST
    // can arrive at any time. Only one at a time (espnow_state.cpp itself
    // only tracks one pending request), tracked here so poll_pair_request()
    // doesn't spawn a second msgbox while one's already open.
    lv_obj_t *s_pair_dialog = nullptr;

    void close_pair_dialog()
    {
        if (s_pair_dialog == nullptr)
            return;
        lv_msgbox_close(s_pair_dialog);
        s_pair_dialog = nullptr;
    }

    // Shown after tapping "Pair" - sending credentials isn't proof the pod
    // received them, so this waits for its SM_PROVISION_ACK (or times out)
    // before claiming success. See espnow_state.h's SM_PairOutcome.
    lv_obj_t *s_ack_dialog = nullptr;
    lv_obj_t *s_ack_text = nullptr;
    uint32_t s_ack_dialog_close_at_ms = 0; // 0 = not scheduled to auto-close

    void close_ack_dialog()
    {
        if (s_ack_dialog == nullptr)
            return;
        lv_msgbox_close(s_ack_dialog);
        s_ack_dialog = nullptr;
        s_ack_dialog_close_at_ms = 0;
    }

    void on_ack_dialog_ok_clicked(lv_event_t *)
    {
        close_ack_dialog();
    }

    void show_ack_dialog()
    {
        s_ack_dialog = lv_msgbox_create(nullptr);
        lv_obj_set_width(s_ack_dialog, 560);

        lv_obj_t *title = lv_msgbox_add_title(s_ack_dialog, "Pairing");
        lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);

        s_ack_text = lv_msgbox_add_text(s_ack_dialog, "Sent the mesh channel - waiting for the pod to confirm...");
        lv_obj_set_style_text_font(s_ack_text, &lv_font_montserrat_24, 0);
    }

    // Polls espnow_pair_outcome() while s_ack_dialog is open and updates it
    // once resolved - confirmed (auto-closes after a couple seconds) or
    // timed out (adds an OK button, since that's worth the user noticing).
    void poll_ack_dialog(lv_timer_t *)
    {
        if (s_ack_dialog == nullptr)
            return;

        if (s_ack_dialog_close_at_ms != 0)
        {
            if (millis() >= s_ack_dialog_close_at_ms)
                close_ack_dialog();
            return;
        }

        SM_PairOutcome outcome = espnow_pair_outcome();
        if (outcome == SM_PAIR_CONFIRMED)
        {
            lv_label_set_text(s_ack_text, "Paired successfully - the pod will rejoin shortly.");
            espnow_clear_pair_outcome();
            s_ack_dialog_close_at_ms = millis() + 2000;
        }
        else if (outcome == SM_PAIR_TIMED_OUT)
        {
            lv_label_set_text(s_ack_text,
                               "Sent, but no confirmation from the pod - check its LED, or it may need "
                               "another try.");
            espnow_clear_pair_outcome();
            lv_obj_t *ok_btn = lv_msgbox_add_footer_button(s_ack_dialog, "OK");
            lv_obj_set_size(ok_btn, 180, 70);
            lv_obj_set_style_text_font(lv_obj_get_child(ok_btn, 0), &lv_font_montserrat_24, 0);
            lv_obj_add_event_cb(ok_btn, on_ack_dialog_ok_clicked, LV_EVENT_CLICKED, nullptr);
        }
    }

    void on_pair_accept_clicked(lv_event_t *)
    {
        espnow_accept_pair_request();
        close_pair_dialog();
        show_ack_dialog();
    }

    void on_pair_decline_clicked(lv_event_t *)
    {
        espnow_reject_pair_request();
        close_pair_dialog();
    }

    // Polls for an incoming pair request and shows a Yes/No confirmation -
    // the CYD never has to leave its own network to receive these (see
    // ESPNOW_PROTOCOL.md's "Wi-Fi provisioning" section), so this can just
    // run continuously in the background.
    void poll_pair_request(lv_timer_t *)
    {
        if (s_pair_dialog != nullptr)
            return; // already showing one - espnow_state.cpp won't track a second anyway

        if (!espnow_has_incoming_pair_request())
            return;

        const SM_IncomingPairRequest *req = espnow_incoming_pair_request();

        s_pair_dialog = lv_msgbox_create(nullptr); // NULL parent = modal, auto top-layer
        // LVGL's default msgbox width is only 2*LV_DPI_DEF (260px on this
        // config) with correspondingly tiny ~43px-wide footer buttons -
        // easy to miss on an 800x480 panel, which is almost certainly why
        // tapping "Pair" wasn't registering (touches landing just outside
        // the button, on the modal backdrop instead). Roughly double both.
        lv_obj_set_width(s_pair_dialog, 560);

        lv_obj_t *title = lv_msgbox_add_title(s_pair_dialog, "New FN 2-Wire Pod");
        lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);

        lv_obj_t *text = lv_msgbox_add_text_fmt(s_pair_dialog, "\"%s\" (%s) wants to join your ESP-NOW mesh. Pair it?",
                                                 req->friendlyName, espnow_device_type_name(req->deviceType));
        lv_obj_set_style_text_font(text, &lv_font_montserrat_24, 0);

        lv_obj_t *decline_btn = lv_msgbox_add_footer_button(s_pair_dialog, "Ignore");
        lv_obj_set_size(decline_btn, 180, 70);
        lv_obj_set_style_text_font(lv_obj_get_child(decline_btn, 0), &lv_font_montserrat_24, 0);
        lv_obj_add_event_cb(decline_btn, on_pair_decline_clicked, LV_EVENT_CLICKED, nullptr);

        lv_obj_t *accept_btn = lv_msgbox_add_footer_button(s_pair_dialog, "Pair");
        lv_obj_set_size(accept_btn, 180, 70);
        lv_obj_set_style_text_font(lv_obj_get_child(accept_btn, 0), &lv_font_montserrat_24, 0);
        lv_obj_add_event_cb(accept_btn, on_pair_accept_clicked, LV_EVENT_CLICKED, nullptr);
    }

    // RSSI (dBm, more negative = weaker) -> how many of the 4 bars light up.
    // Thresholds are the common rule-of-thumb breakpoints for Wi-Fi signal
    // quality (-55/-65/-75dBm), not anything from a spec.
    int signal_level_from_rssi(int32_t rssi)
    {
        if (rssi >= -55)
            return 4;
        if (rssi >= -65)
            return 3;
        if (rssi >= -75)
            return 2;
        return 1;
    }

    // level 0 (disconnected) = all bars dim grey; each additional level lights
    // one more bar green, bottom-up - so "connected" always reads as at least
    // one green bar, and signal strength is however many more are lit.
    void update_signal_bars(int level)
    {
        static int s_last_level = -1;
        if (level == s_last_level)
            return;
        s_last_level = level;

        lv_color_t on = lv_palette_main(LV_PALETTE_GREEN);
        lv_color_t off = lv_palette_darken(LV_PALETTE_GREY, 2);
        lv_obj_set_style_bg_color(s_wifi_bar1, level >= 1 ? on : off, 0);
        lv_obj_set_style_bg_color(s_wifi_bar2, level >= 2 ? on : off, 0);
        lv_obj_set_style_bg_color(s_wifi_bar3, level >= 3 ? on : off, 0);
        lv_obj_set_style_bg_color(s_wifi_bar4, level >= 4 ? on : off, 0);
    }

    void set_wifi_badge_visible(bool visible)
    {
        if (visible)
            lv_obj_clear_flag(s_wifi_badge, LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_add_flag(s_wifi_badge, LV_OBJ_FLAG_HIDDEN);
    }

    // "Not connected" is a small red X badge layered over the bars.
    void poll_wifi_status(lv_timer_t *)
    {
        bool connected = WiFi.status() == WL_CONNECTED;

        if (connected != s_wifi_was_connected)
        {
            s_wifi_was_connected = connected;
            set_wifi_badge_visible(!connected);
        }

        // Runs every tick (not just on connect/disconnect) since RSSI drifts
        // continuously while connected - update_signal_bars() itself skips
        // the redraw if the bar count hasn't actually changed.
        update_signal_bars(connected ? signal_level_from_rssi(WiFi.RSSI()) : 0);
    }

    // Single click toggles: connected -> disconnect, not connected ->
    // reconnect to the last-used network. Updates the badge/bars immediately
    // here instead of waiting up to 1s for poll_wifi_status's next tick -
    // this matters most on the disconnect direction, since
    // WiFi.disconnect(true) drops the association synchronously so the real
    // state is already known by the time this handler returns. Reconnecting
    // is inherently asynchronous (association/DHCP take real time), so the
    // badge is left showing "disconnected" until poll_wifi_status observes
    // the actual flip to connected.
    void on_wifi_icon_clicked(lv_event_t *)
    {
        if (WiFi.status() == WL_CONNECTED)
        {
            app_wifi_disable();

            s_wifi_was_connected = false;
            set_wifi_badge_visible(true);
            update_signal_bars(0);
        }
        else
        {
            g_config.wifi_enabled = true;
            config_save(g_config);
            app_wifi_apply();
        }
    }

    void on_menu_clicked(lv_event_t *)
    {
        lv_obj_clear_flag(s_drawer, LV_OBJ_FLAG_HIDDEN);
    }

    void on_drawer_close_clicked(lv_event_t *)
    {
        lv_obj_add_flag(s_drawer, LV_OBJ_FLAG_HIDDEN);
    }

    void on_drawer_wifi_clicked(lv_event_t *)
    {
        lv_obj_add_flag(s_drawer, LV_OBJ_FLAG_HIDDEN);
        ui_wifi_setup_show();
    }

    void on_drawer_setup_clicked(lv_event_t *)
    {
        lv_obj_add_flag(s_drawer, LV_OBJ_FLAG_HIDDEN);
        ui_setup_show();
    }

    void on_drawer_espnow_clicked(lv_event_t *)
    {
        lv_obj_add_flag(s_drawer, LV_OBJ_FLAG_HIDDEN);
        ui_espnow_show();
    }

    lv_obj_t *make_drawer_btn(lv_obj_t *parent, const char *icon, const char *text, lv_event_cb_t cb)
    {
        lv_obj_t *btn = lv_button_create(parent);
        lv_obj_set_width(btn, LV_PCT(100));
        lv_obj_set_height(btn, 80);

        // Icon + label side by side, both vertically centered on the button -
        // a flex row rather than two independently-aligned children so the
        // pair stays centered as a unit regardless of icon glyph width.
        lv_obj_t *row = lv_obj_create(btn);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(row, 12, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_center(row);

        lv_obj_t *icon_lbl = lv_label_create(row);
        lv_label_set_text(icon_lbl, icon);
        lv_obj_set_style_text_font(icon_lbl, &lv_font_montserrat_28, 0);

        lv_obj_t *text_lbl = lv_label_create(row);
        lv_label_set_text(text_lbl, text);
        lv_obj_set_style_text_font(text_lbl, &lv_font_montserrat_28, 0);

        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, nullptr);
        return btn;
    }
}

void ui_shell_create(lv_obj_t *parent)
{
    lv_obj_t *top_bar = lv_obj_create(parent);
    lv_obj_remove_style_all(top_bar);
    lv_obj_set_width(top_bar, LV_PCT(100));
    lv_obj_set_height(top_bar, 56);
    lv_obj_set_style_pad_all(top_bar, 8, 0);
    lv_obj_set_style_bg_color(top_bar, lv_palette_darken(LV_PALETTE_GREY, 3), 0);
    lv_obj_set_style_bg_opa(top_bar, LV_OPA_COVER, 0);
    lv_obj_clear_flag(top_bar, LV_OBJ_FLAG_SCROLLABLE);

    // Manual alignment, not a flex row: the title label needs to sit
    // dead-center on the bar regardless of the menu button and Wi-Fi icon's
    // differing widths, which flex's space-between only approximates.
    lv_obj_t *menu_btn = lv_button_create(top_bar);
    lv_obj_set_size(menu_btn, 48, 48);
    lv_obj_align(menu_btn, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_opa(menu_btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(menu_btn, LV_OPA_TRANSP, LV_STATE_PRESSED);
    lv_obj_set_style_shadow_opa(menu_btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(menu_btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_outline_opa(menu_btn, LV_OPA_TRANSP, 0);
    lv_obj_t *menu_icon = lv_label_create(menu_btn);
    lv_label_set_text(menu_icon, LV_SYMBOL_LIST);
    lv_obj_set_style_text_color(menu_icon, lv_color_white(), 0);
    lv_obj_set_style_text_font(menu_icon, &lv_font_montserrat_28, 0);
    lv_obj_center(menu_icon);
    lv_obj_add_event_cb(menu_btn, on_menu_clicked, LV_EVENT_CLICKED, nullptr);

    s_title_label = lv_label_create(top_bar);
    lv_label_set_text(s_title_label, kAppTitle);
    lv_obj_set_style_text_color(s_title_label, lv_color_white(), 0);
    // Largest montserrat size (34, line height 38px) that still fits inside
    // the bar's 56px height minus its 8px top/bottom padding (40px content).
    lv_obj_set_style_text_font(s_title_label, &lv_font_montserrat_34, 0);
    lv_obj_align(s_title_label, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *wifi_wrap = lv_obj_create(top_bar);
    lv_obj_remove_style_all(wifi_wrap);
    lv_obj_set_size(wifi_wrap, 32, 32);
    lv_obj_align(wifi_wrap, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_clear_flag(wifi_wrap, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(wifi_wrap, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(wifi_wrap, on_wifi_icon_clicked, LV_EVENT_CLICKED, nullptr);

    // Custom 3-bar signal graphic (not the single-glyph LV_SYMBOL_WIFI icon)
    // so each bar can be colored independently by signal strength - bottom-
    // aligned so bars of increasing height sit on a common baseline.
    auto make_signal_bar = [&](int16_t w, int16_t h, int16_t x)
    {
        lv_obj_t *bar = lv_obj_create(wifi_wrap);
        lv_obj_remove_style_all(bar);
        lv_obj_set_size(bar, w, h);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(bar, lv_palette_darken(LV_PALETTE_GREY, 2), 0);
        lv_obj_align(bar, LV_ALIGN_BOTTOM_LEFT, x, -3);
        return bar;
    };
    s_wifi_bar1 = make_signal_bar(5, 6, 3);
    s_wifi_bar2 = make_signal_bar(5, 12, 10);
    s_wifi_bar3 = make_signal_bar(5, 18, 17);
    s_wifi_bar4 = make_signal_bar(5, 24, 24);

    s_wifi_badge = lv_label_create(wifi_wrap);
    lv_label_set_text(s_wifi_badge, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_color(s_wifi_badge, lv_palette_main(LV_PALETTE_RED), 0);
    lv_obj_align(s_wifi_badge, LV_ALIGN_BOTTOM_RIGHT, 2, 2);

    // s_wifi_was_connected defaults to false, matching the UI's initial
    // assumed-disconnected state (badge visible) - so the first tick's
    // connected != s_wifi_was_connected check always fires correctly,
    // whether Wi-Fi is still connecting or already connected by then (e.g.
    // fast reconnect with cached credentials beating the first 1000ms tick).
    // Seeding this true instead would make a same-tick "already connected"
    // case look like no change, leaving the badge stuck wrong until a later
    // real disconnect/reconnect edge.
    lv_timer_create(poll_wifi_status, 1000, nullptr);
    lv_timer_create(poll_pair_request, 1000, nullptr);
    lv_timer_create(poll_ack_dialog, 250, nullptr);
}

void ui_shell_build_pool()
{
    s_drawer = lv_obj_create(lv_layer_top());
    lv_obj_add_flag(s_drawer, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_size(s_drawer, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_drawer, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_drawer, LV_OPA_90, 0);
    lv_obj_set_style_pad_all(s_drawer, 16, 0);
    lv_obj_clear_flag(s_drawer, LV_OBJ_FLAG_SCROLLABLE);

    // Back button in the upper-left corner, matching every other full-page
    // overlay in this app, instead of a "Close" button at the bottom.
    lv_obj_t *back_btn = lv_button_create(s_drawer);
    lv_obj_set_size(back_btn, 100, 56);
    lv_obj_align(back_btn, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_t *back_lbl = lv_label_create(back_btn);
    lv_label_set_text(back_lbl, "Back");
    lv_obj_set_style_text_font(back_lbl, &lv_font_montserrat_24, 0);
    lv_obj_center(back_lbl);
    lv_obj_add_event_cb(back_btn, on_drawer_close_clicked, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *btn_wrap = lv_obj_create(s_drawer);
    lv_obj_remove_style_all(btn_wrap);
    lv_obj_set_width(btn_wrap, 440);
    lv_obj_set_height(btn_wrap, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(btn_wrap, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(btn_wrap, 10, 0);
    lv_obj_align(btn_wrap, LV_ALIGN_CENTER, 0, 0);

    make_drawer_btn(btn_wrap, LV_SYMBOL_WIFI, "Wi-Fi", on_drawer_wifi_clicked);
    make_drawer_btn(btn_wrap, LV_SYMBOL_BLUETOOTH, "Esp Now", on_drawer_espnow_clicked);
    make_drawer_btn(btn_wrap, LV_SYMBOL_SETTINGS, "Setup", on_drawer_setup_clicked);
}

void ui_shell_set_status_override(const char *text)
{
    lv_label_set_text(s_title_label, text);
}

void ui_shell_clear_status_override()
{
    lv_label_set_text(s_title_label, kAppTitle);
}

lv_obj_t *ui_shell_get_title_label()
{
    return s_title_label;
}
