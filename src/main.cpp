#include <Arduino.h>
#include <ArduinoOTA.h>
#include <esp_task_wdt.h>
#include <lvgl.h>

#include "lgfx_config.h"

#include "app_state.h"
#include "config.h"
#include "ui_boot_menu.h"
#include "ui_boot_splash.h"
#include "espnow_state.h"
#include "ui_capture_learn.h"
#include "ui_espnow.h"
#include "ui_fn_main.h"
#include "ui_fn_output.h"
#include "ui_home.h"
#include "ui_setup.h"
#include "ui_shell.h"
#include "ui_wifi_setup.h"

size_t getArduinoLoopTaskStackSize(void)
{
    return 32768;
}

namespace
{
    LGFX lcd;

    // Full-frame buffers (not partial/tiled): LVGL's software renderer has a
    // crash bug where drawing a widget's border can compute an out-of-bounds
    // offset if the border's geometry doesn't fit cleanly within one partial
    // render slice (confirmed via a decoded crash backtrace - lv_draw_rect ->
    // lv_draw_sw_border -> lv_memset, StoreProhibited/null write). A
    // full-frame buffer sidesteps that entirely. Cheap given 8MB of PSRAM.
    constexpr size_t kBufBytes = 800 * 480 * 2;
    uint8_t *s_buf1 = nullptr;
    uint8_t *s_buf2 = nullptr;
    uint32_t s_last_tick = 0;

    // Boot-recovery-menu touch check: watches for a *fresh* touch within the
    // first 8s of loop() running (see ui_boot_menu.h for why it has to be a
    // new press-after-boot rather than a press held through reset - the
    // GT911 calibrates its baseline against whatever's on the glass at its
    // own init time, so a pre-existing hold is invisible to it).
    bool s_boot_check_active = true;
    uint32_t s_boot_check_start = 0;

    // Backlight screen saver: any touch resets s_last_activity_ms and wakes
    // the backlight; loop() blanks it after g_config.screensaver_timeout_sec
    // of no touches, if enabled. 128 matches the brightness set at boot.
    constexpr uint8_t kBacklightOn = 128;
    uint32_t s_last_activity_ms = 0;
    bool s_backlight_off = false;

    // Jumping the backlight PWM duty straight from 0 to a mid-range value in
    // one lcd.setBrightness() call reliably browned out this board on wake
    // from the screensaver (confirmed via a live serial capture: the reset
    // landed synchronously inside that single call, every time, only on the
    // 0->on direction - the on->0 blank direction never failed). The
    // backlight LEDs' inrush current spikes hard on a sudden duty jump;
    // stepping through intermediate values spreads it out instead.
    void ramp_backlight_to(uint8_t target)
    {
        constexpr uint8_t kSteps = 16;
        for (uint8_t i = 1; i <= kSteps; i++)
        {
            lcd.setBrightness(static_cast<uint8_t>((static_cast<uint16_t>(target) * i) / kSteps));
            delay(6);
        }
    }

    void disp_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
    {
        int32_t w = area->x2 - area->x1 + 1;
        int32_t h = area->y2 - area->y1 + 1;
        lcd.startWrite();
        lcd.setAddrWindow(area->x1, area->y1, w, h);
        lcd.writePixels(reinterpret_cast<lgfx::rgb565_t *>(px_map), w * h);
        lcd.endWrite();
        lv_display_flush_ready(disp);
    }

    void touch_read_cb(lv_indev_t *, lv_indev_data_t *data)
    {
        int32_t x, y;
        if (lcd.getTouch(&x, &y))
        {
            // The GT911 on this board self-reports a native resolution of
            // 480x272 regardless of the actual 800x480 panel (confirmed via
            // its info registers), and LovyanGFX's driver doesn't rescale for
            // us - raw coordinates come back in that smaller native range.
            // Scale them up to display coordinates.
            data->point.x = x * 799 / 479;
            data->point.y = y * 479 / 271;
            data->state = LV_INDEV_STATE_PRESSED;

            s_last_activity_ms = millis();
            if (s_backlight_off)
            {
                ramp_backlight_to(kBacklightOn);
                s_backlight_off = false;
            }
        }
        else
        {
            data->state = LV_INDEV_STATE_RELEASED;
        }
    }

    // Blanks the backlight after g_config.screensaver_timeout_sec of no
    // touch activity; a fresh touch (touch_read_cb above) wakes it back up.
    // That first waking touch is also delivered to LVGL as a normal press -
    // it isn't "consumed" purely as a wake gesture.
    //
    // Reads millis() itself rather than taking loop()'s cached `now`:
    // touch_read_cb() runs *inside* lv_timer_handler(), which happens after
    // that cached value is captured, so a touch this same iteration can
    // leave s_last_activity_ms newer than the stale `now` - the unsigned
    // subtraction below would then underflow to a huge value and instantly
    // re-blank on the very touch that was supposed to wake the screen
    // (confirmed - this was why touching never seemed to reset the timer).
    void check_screensaver()
    {
        if (!g_config.screensaver_enabled || s_backlight_off)
            return;
        uint32_t timeoutMs = static_cast<uint32_t>(g_config.screensaver_timeout_sec) * 1000;
        if (millis() - s_last_activity_ms >= timeoutMs)
        {
            lcd.setBrightness(0);
            s_backlight_off = true;
        }
    }
}

void setup()
{
    Serial.begin(115200);

    lcd.init();
    lcd.setBrightness(128);

    lv_init();

    lv_display_t *disp = lv_display_create(800, 480);
    s_buf1 = static_cast<uint8_t *>(heap_caps_malloc(kBufBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    s_buf2 = static_cast<uint8_t *>(heap_caps_malloc(kBufBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    lv_display_set_buffers(disp, s_buf1, s_buf2, kBufBytes, LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(disp, disp_flush_cb);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);

    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touch_read_cb);
    lv_indev_set_display(indev, disp);
    ui_boot_menu_set_indev(indev);

    config_load(g_config);

    lv_obj_t *root = lv_screen_active();
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(root, 0, 0);
    // pad_all() only zeroes the 4 edge insets, not the flex gap - the
    // default theme's screen style sets a nonzero pad_row (~14-24px)
    // between flex children, which otherwise leaves a strip of the theme's
    // light background color visible between the top bar and the tabview
    // below it.
    lv_obj_set_style_pad_row(root, 0, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    ui_shell_create(root);

    lv_obj_t *tabview = lv_tabview_create(root);
    lv_obj_set_flex_grow(tabview, 1);
    lv_tabview_set_tab_bar_position(tabview, LV_DIR_BOTTOM);
    lv_tabview_set_tab_bar_size(tabview, 50);

    // Starting point for whatever this app ends up showing - add more tabs
    // here as features land.
    lv_obj_t *tab_home = lv_tabview_add_tab(tabview, "Home");
    ui_home_create(tab_home);

    ui_wifi_setup_build_pool();
    ui_setup_build_pool();
    ui_espnow_build_pool();
    ui_fn_main_build_pool();
    ui_fn_output_build_pool();
    ui_capture_learn_build_pool();
    ui_shell_build_pool();
    ui_boot_menu_build_pool();
    ui_boot_splash_build_pool();

    ui_home_start();

    app_sd_init();
    app_fs_init();
    app_wifi_apply();
    // Requires Wi-Fi already in STA mode (which app_wifi_apply() just set,
    // regardless of whether the actual join succeeds) - doesn't need an
    // active connection itself, just the radio mode.
    if (espnow_init())
        espnow_start_messaging();

    // OTA firmware updates (pio run -t upload --upload-port <device-ip>,
    // or espota.py directly) - reachable once Wi-Fi actually connects;
    // ArduinoOTA.begin() itself is safe to call regardless of current
    // connection state, it just starts listening once there's an IP.
    ArduinoOTA.setHostname("cydfn");
    ArduinoOTA.setPassword("5512");
    ArduinoOTA.onProgress([](unsigned int, unsigned int)
                           {
        // ArduinoOTA.handle() (in loop()) blocks synchronously for the
        // whole transfer once one starts - a ~3MB image over Wi-Fi easily
        // takes longer than the 15s watchdog timeout below, so this keeps
        // it fed for the duration instead of rebooting mid-flash.
        esp_task_wdt_reset(); });
    ArduinoOTA.begin();

    ui_boot_splash_start();

    s_boot_check_start = millis();
    s_last_tick = s_boot_check_start;
    s_last_activity_ms = s_boot_check_start;

    // Watchdog: if loop() ever hangs, this reboots instead of sitting frozen.
    // 15s (not 3s): the ESP32 WiFi driver's own connection-failure/retry
    // handling (e.g. repeated ASSOC_FAIL against a stale saved password) can
    // legitimately block the calling task for several seconds - confirmed by
    // a watchdog firing with IDLE, not loopTask, as the running task, i.e.
    // loopTask was blocked waiting on the driver, not stuck in our own code.
    esp_task_wdt_init(15, true);
    esp_task_wdt_add(nullptr);
}

void loop()
{
    esp_task_wdt_reset();
    ArduinoOTA.handle();

    uint32_t const now = millis();
    lv_tick_inc(now - s_last_tick);
    s_last_tick = now;
    lv_timer_handler();

    if (s_boot_check_active)
    {
        int32_t x, y;
        if (lcd.getTouch(&x, &y))
        {
            ui_boot_splash_hide();
            ui_boot_menu_show();
            s_boot_check_active = false;
        }
        else if (now - s_boot_check_start > 2000)
        {
            s_boot_check_active = false;
        }
    }

    check_screensaver();

    delay(5);
}
