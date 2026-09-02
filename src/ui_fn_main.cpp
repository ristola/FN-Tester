#include "ui_fn_main.h"

namespace
{
    lv_obj_t *s_overlay = nullptr;

    void on_back_clicked(lv_event_t *)
    {
        lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
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
    lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *placeholder = lv_label_create(body);
    lv_label_set_text(placeholder,
                       "Listen to FN-MAIN and report recognizable/valid FN frames.\n\nNot implemented yet.");
    lv_obj_set_style_text_color(placeholder, lv_palette_darken(LV_PALETTE_GREY, 1), 0);
    lv_obj_set_style_text_font(placeholder, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_align(placeholder, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(placeholder);
}

void ui_fn_main_show()
{
    lv_obj_clear_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
}
