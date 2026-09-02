#include "ui_setup.h"

#include "app_state.h"

namespace
{
    lv_obj_t *s_overlay = nullptr;

    lv_obj_t *s_screensaver_switch = nullptr;
    lv_obj_t *s_screensaver_delay_btn = nullptr;
    lv_obj_t *s_screensaver_delay_lbl = nullptr;

    lv_obj_t *s_ss_entry_overlay = nullptr;
    lv_obj_t *s_ss_entry_ta = nullptr;
    lv_obj_t *s_ss_entry_keyboard = nullptr;

    void on_back_clicked(lv_event_t *)
    {
        lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
    }

    constexpr int kMinScreensaverSec = 5;
    constexpr int kMaxScreensaverSec = 3600;

    void update_screensaver_delay_label()
    {
        lv_label_set_text_fmt(s_screensaver_delay_lbl, "%d sec", g_config.screensaver_timeout_sec);
    }

    void on_screensaver_switch_changed(lv_event_t *e)
    {
        lv_obj_t *sw = static_cast<lv_obj_t *>(lv_event_get_target(e));
        g_config.screensaver_enabled = lv_obj_has_state(sw, LV_STATE_CHECKED);
        config_save(g_config);

        if (g_config.screensaver_enabled)
            lv_obj_clear_state(s_screensaver_delay_btn, LV_STATE_DISABLED);
        else
            lv_obj_add_state(s_screensaver_delay_btn, LV_STATE_DISABLED);
    }

    // Dedicated full-page entry pattern (like the Wi-Fi password page), but
    // with a numeric keyboard - built-in LV_KEYBOARD_MODE_NUMBER, not a
    // custom key map (a custom map with a null ctrl_map crashed elsewhere in
    // this app; the built-in numeric mode is proven safe).
    void on_screensaver_delay_clicked(lv_event_t *)
    {
        char buf[8];
        snprintf(buf, sizeof(buf), "%d", g_config.screensaver_timeout_sec);
        lv_textarea_set_text(s_ss_entry_ta, buf);
        lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_ss_entry_overlay, LV_OBJ_FLAG_HIDDEN);
    }

    void on_ss_entry_cancel_clicked(lv_event_t *)
    {
        lv_obj_add_flag(s_ss_entry_overlay, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
    }

    // Listens on the textarea, not the keyboard - LVGL's keyboard has two
    // "confirm" keys with different targets: the return-arrow key (end of
    // the middle letter row) fires LV_EVENT_READY only on the bound
    // textarea, while the small checkmark/OK key (bottom row) fires it on
    // the keyboard AND the textarea. Listening on the textarea catches both.
    void on_ss_entry_ready(lv_event_t *)
    {
        int sec = atoi(lv_textarea_get_text(s_ss_entry_ta));
        if (sec < kMinScreensaverSec)
            sec = kMinScreensaverSec;
        if (sec > kMaxScreensaverSec)
            sec = kMaxScreensaverSec;

        g_config.screensaver_timeout_sec = sec;
        config_save(g_config);
        update_screensaver_delay_label();

        lv_obj_add_flag(s_ss_entry_overlay, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
    }

    // Shared 3-slot header (left button, centered title, optional right
    // button) - manual alignment, not a flex row with space-between, so the
    // title stays dead-center regardless of the side buttons' widths.
    lv_obj_t *make_header(lv_obj_t *parent, const char *title_text)
    {
        lv_obj_t *header = lv_obj_create(parent);
        lv_obj_remove_style_all(header);
        lv_obj_set_width(header, LV_PCT(100));
        lv_obj_set_height(header, 56);
        lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *title = lv_label_create(header);
        lv_label_set_text(title, title_text);
        lv_obj_set_style_text_color(title, lv_color_white(), 0);
        lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
        lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

        return header;
    }
}

void ui_setup_build_pool()
{
    s_overlay = lv_obj_create(lv_layer_top());
    lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_size(s_overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_overlay, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(s_overlay, 16, 0);
    lv_obj_set_flex_flow(s_overlay, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_overlay, 12, 0);
    lv_obj_clear_flag(s_overlay, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *header_row = make_header(s_overlay, "Setup");
    lv_obj_t *back_btn = lv_button_create(header_row);
    lv_obj_set_size(back_btn, 100, 56);
    lv_obj_align(back_btn, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_t *back_lbl = lv_label_create(back_btn);
    lv_label_set_text(back_lbl, "Back");
    lv_obj_set_style_text_font(back_lbl, &lv_font_montserrat_24, 0);
    lv_obj_center(back_lbl);
    lv_obj_add_event_cb(back_btn, on_back_clicked, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *screensaver_row = lv_obj_create(s_overlay);
    lv_obj_remove_style_all(screensaver_row);
    lv_obj_set_width(screensaver_row, LV_PCT(100));
    lv_obj_set_height(screensaver_row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(screensaver_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(screensaver_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(screensaver_row, 10, 0);

    lv_obj_t *ss_label = lv_label_create(screensaver_row);
    lv_label_set_text(ss_label, "Screen Saver:");
    lv_obj_set_style_text_color(ss_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(ss_label, &lv_font_montserrat_24, 0);

    s_screensaver_switch = lv_switch_create(screensaver_row);
    lv_obj_add_event_cb(s_screensaver_switch, on_screensaver_switch_changed, LV_EVENT_VALUE_CHANGED, nullptr);

    lv_obj_t *ss_spacer = lv_obj_create(screensaver_row);
    lv_obj_remove_style_all(ss_spacer);
    lv_obj_set_flex_grow(ss_spacer, 1);

    lv_obj_t *ss_delay_label = lv_label_create(screensaver_row);
    lv_label_set_text(ss_delay_label, "Delay:");
    lv_obj_set_style_text_color(ss_delay_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(ss_delay_label, &lv_font_montserrat_24, 0);

    s_screensaver_delay_btn = lv_button_create(screensaver_row);
    lv_obj_set_size(s_screensaver_delay_btn, 150, 56);
    s_screensaver_delay_lbl = lv_label_create(s_screensaver_delay_btn);
    lv_label_set_text(s_screensaver_delay_lbl, "60 sec");
    lv_obj_set_style_text_font(s_screensaver_delay_lbl, &lv_font_montserrat_24, 0);
    lv_obj_center(s_screensaver_delay_lbl);
    lv_obj_add_event_cb(s_screensaver_delay_btn, on_screensaver_delay_clicked, LV_EVENT_CLICKED, nullptr);

    // Dedicated full-page numeric entry screen for the screen saver delay.
    s_ss_entry_overlay = lv_obj_create(lv_layer_top());
    lv_obj_add_flag(s_ss_entry_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_size(s_ss_entry_overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_ss_entry_overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_ss_entry_overlay, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(s_ss_entry_overlay, 16, 0);
    lv_obj_set_flex_flow(s_ss_entry_overlay, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_ss_entry_overlay, 12, 0);
    lv_obj_clear_flag(s_ss_entry_overlay, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *ss_entry_header = make_header(s_ss_entry_overlay, "Enter Delay (sec)");
    lv_obj_t *ss_cancel_btn = lv_button_create(ss_entry_header);
    lv_obj_set_size(ss_cancel_btn, 120, 56);
    lv_obj_align(ss_cancel_btn, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_t *ss_cancel_lbl = lv_label_create(ss_cancel_btn);
    lv_label_set_text(ss_cancel_lbl, "Cancel");
    lv_obj_set_style_text_font(ss_cancel_lbl, &lv_font_montserrat_24, 0);
    lv_obj_center(ss_cancel_lbl);
    lv_obj_add_event_cb(ss_cancel_btn, on_ss_entry_cancel_clicked, LV_EVENT_CLICKED, nullptr);

    s_ss_entry_ta = lv_textarea_create(s_ss_entry_overlay);
    lv_obj_set_width(s_ss_entry_ta, LV_PCT(100));
    lv_textarea_set_one_line(s_ss_entry_ta, true);
    lv_obj_set_style_text_font(s_ss_entry_ta, &lv_font_montserrat_28, 0);

    s_ss_entry_keyboard = lv_keyboard_create(s_ss_entry_overlay);
    lv_keyboard_set_mode(s_ss_entry_keyboard, LV_KEYBOARD_MODE_NUMBER);
    lv_keyboard_set_textarea(s_ss_entry_keyboard, s_ss_entry_ta);
    lv_obj_set_flex_grow(s_ss_entry_keyboard, 1);
    lv_obj_add_event_cb(s_ss_entry_ta, on_ss_entry_ready, LV_EVENT_READY, nullptr);
}

void ui_setup_show()
{
    if (g_config.screensaver_enabled)
    {
        lv_obj_add_state(s_screensaver_switch, LV_STATE_CHECKED);
        lv_obj_clear_state(s_screensaver_delay_btn, LV_STATE_DISABLED);
    }
    else
    {
        lv_obj_clear_state(s_screensaver_switch, LV_STATE_CHECKED);
        lv_obj_add_state(s_screensaver_delay_btn, LV_STATE_DISABLED);
    }
    update_screensaver_delay_label();

    lv_obj_add_flag(s_ss_entry_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
}
