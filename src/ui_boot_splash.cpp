#include "ui_boot_splash.h"

#include "generated/boot_logo.h"

namespace
{
    constexpr uint32_t kSplashDurationMs = 4500;

    lv_obj_t *s_overlay = nullptr;
    lv_timer_t *s_timer = nullptr;

    void on_splash_timeout(lv_timer_t *)
    {
        lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
        s_timer = nullptr; // one-shot: LVGL deletes it after this call returns
    }
}

void ui_boot_splash_build_pool()
{
    s_overlay = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_overlay);
    lv_obj_set_size(s_overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_overlay, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_overlay, LV_OBJ_FLAG_SCROLLABLE);

    // Logo is pre-composited onto a transparent 800x480 canvas at conversion
    // time (centered, margins included), so it draws directly at (0,0) and
    // blends into this overlay's black background via real alpha.
    lv_obj_t *logo = lv_image_create(s_overlay);
    lv_image_set_src(logo, &boot_logo);
    lv_obj_set_pos(logo, 0, 0);
}

void ui_boot_splash_start()
{
    s_timer = lv_timer_create(on_splash_timeout, kSplashDurationMs, nullptr);
    lv_timer_set_repeat_count(s_timer, 1);
}

void ui_boot_splash_hide()
{
    lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
    if (s_timer != nullptr)
    {
        lv_timer_delete(s_timer);
        s_timer = nullptr;
    }
}
