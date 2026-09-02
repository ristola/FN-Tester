#include "ui_home.h"

#include "ui_capture_learn.h"
#include "ui_fn_main.h"
#include "ui_fn_output.h"

namespace
{
    void on_fn_main_clicked(lv_event_t *)
    {
        ui_fn_main_show();
    }

    void on_fn_output_clicked(lv_event_t *)
    {
        ui_fn_output_show();
    }

    void on_capture_learn_clicked(lv_event_t *)
    {
        ui_capture_learn_show();
    }

    // Square icon-over-label tile for the Home tab's mode grid - bigger and
    // more tappable than the drawer's list-style rows (make_drawer_btn in
    // ui_shell.cpp), since these are the app's primary actions rather than
    // secondary settings.
    lv_obj_t *make_mode_tile(lv_obj_t *parent, const char *icon, const char *text, lv_event_cb_t cb)
    {
        lv_obj_t *tile = lv_button_create(parent);
        lv_obj_set_size(tile, 180, 160);

        lv_obj_t *col = lv_obj_create(tile);
        lv_obj_remove_style_all(col);
        lv_obj_set_size(col, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(col, 10, 0);
        lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_center(col);

        lv_obj_t *icon_lbl = lv_label_create(col);
        lv_label_set_text(icon_lbl, icon);
        lv_obj_set_style_text_font(icon_lbl, &lv_font_montserrat_34, 0);

        lv_obj_t *text_lbl = lv_label_create(col);
        lv_label_set_text(text_lbl, text);
        lv_obj_set_style_text_font(text_lbl, &lv_font_montserrat_24, 0);

        lv_obj_add_event_cb(tile, cb, LV_EVENT_CLICKED, nullptr);
        return tile;
    }
}

void ui_home_create(lv_obj_t *parent)
{
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(parent, 8, 0);
    lv_obj_set_style_pad_row(parent, 8, 0);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *tile_row = lv_obj_create(parent);
    lv_obj_remove_style_all(tile_row);
    lv_obj_set_width(tile_row, LV_PCT(100));
    lv_obj_set_flex_grow(tile_row, 1);
    lv_obj_set_flex_flow(tile_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(tile_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(tile_row, 16, 0);
    lv_obj_clear_flag(tile_row, LV_OBJ_FLAG_SCROLLABLE);

    make_mode_tile(tile_row, LV_SYMBOL_CALL, "FN Main", on_fn_main_clicked);
    make_mode_tile(tile_row, LV_SYMBOL_CHARGE, "FN Output", on_fn_output_clicked);
    make_mode_tile(tile_row, LV_SYMBOL_EYE_OPEN, "Capture / Learn", on_capture_learn_clicked);
}

void ui_home_start()
{
}
