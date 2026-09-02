#include "ui_boot_menu.h"

#include <SD.h>
#include <esp_task_wdt.h>

#include "app_state.h"
#include "config.h"

namespace
{
    enum class PendingAction
    {
        None,
        RestoreDefaults,
        FormatSd,
    };

    lv_indev_t *s_indev = nullptr;
    PendingAction s_pending_action = PendingAction::None;

    lv_obj_t *s_overlay = nullptr;
    lv_obj_t *s_message = nullptr;
    lv_obj_t *s_menu_row = nullptr;
    lv_obj_t *s_confirm_row = nullptr;
    lv_obj_t *s_confirm_btn = nullptr;
    lv_obj_t *s_confirm_lbl = nullptr;

    void show_menu()
    {
        s_pending_action = PendingAction::None;
        lv_obj_add_flag(s_message, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_confirm_row, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_menu_row, LV_OBJ_FLAG_HIDDEN);
    }

    void show_confirm(PendingAction action, const char *message, const char *confirmText)
    {
        s_pending_action = action;
        lv_label_set_text(s_message, message);
        lv_label_set_text(s_confirm_lbl, confirmText);
        lv_obj_clear_flag(s_message, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_menu_row, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_confirm_row, LV_OBJ_FLAG_HIDDEN);
    }

    // Deletes every file/directory under `path` (but not `path` itself).
    // Not a true low-level FAT format (SD.h doesn't expose one) - this just
    // empties the card. Resets the watchdog periodically since a card with
    // many files could otherwise take longer than our timeout to walk.
    void delete_recursive(const String &path)
    {
        File dir = SD.open(path);
        if (!dir || !dir.isDirectory())
        {
            if (dir)
                dir.close();
            return;
        }

        File entry = dir.openNextFile();
        while (entry)
        {
            esp_task_wdt_reset();

            String name = entry.name();
            int slash = name.lastIndexOf('/');
            String base = slash >= 0 ? name.substring(slash + 1) : name;
            String full = (path == "/") ? "/" + base : path + "/" + base;
            bool isDir = entry.isDirectory();
            entry.close();

            if (isDir)
            {
                delete_recursive(full);
                SD.rmdir(full);
            }
            else
            {
                SD.remove(full);
            }
            entry = dir.openNextFile();
        }
        dir.close();
    }

    void on_restore_defaults_clicked(lv_event_t *)
    {
        show_confirm(PendingAction::RestoreDefaults,
                      "Erase all settings and reboot?",
                      "Confirm Restore Defaults");
    }

    void on_format_sd_clicked(lv_event_t *)
    {
        if (!g_sd_ready)
        {
            lv_label_set_text(s_message, "SD card not mounted - nothing to erase");
            return;
        }
        show_confirm(PendingAction::FormatSd,
                      "Erase ALL files on the SD card? This is not a low-level\n"
                      "format, just a full wipe - this cannot be undone.",
                      "Confirm Erase SD Card");
    }

    void on_exit_clicked(lv_event_t *)
    {
        lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
    }

    void on_cancel_clicked(lv_event_t *)
    {
        show_menu();
    }

    void on_confirm_clicked(lv_event_t *)
    {
        switch (s_pending_action)
        {
        case PendingAction::RestoreDefaults:
            config_clear();
            lv_label_set_text(s_message, "Settings erased. Rebooting...");
            lv_obj_add_flag(s_confirm_row, LV_OBJ_FLAG_HIDDEN);
            lv_refr_now(nullptr);
            delay(500);
            ESP.restart();
            break;

        case PendingAction::FormatSd:
            lv_label_set_text(s_message, "Erasing SD card...");
            lv_obj_add_flag(s_confirm_row, LV_OBJ_FLAG_HIDDEN);
            lv_refr_now(nullptr);
            delete_recursive("/");
            lv_label_set_text(s_message, "SD card erased.");
            lv_refr_now(nullptr);
            delay(1000);
            show_menu();
            break;

        case PendingAction::None:
            break;
        }
    }

    lv_obj_t *make_menu_btn(lv_obj_t *parent, const char *text, lv_event_cb_t cb)
    {
        lv_obj_t *btn = lv_button_create(parent);
        lv_obj_set_width(btn, LV_PCT(100));
        lv_obj_set_height(btn, 80);
        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, text);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_28, 0);
        lv_obj_center(lbl);
        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, nullptr);
        return btn;
    }

    int s_release_streak = 0;

    // Requires several consecutive RELEASED reads, not just one, before
    // revealing the menu: LVGL can retarget an in-progress press to
    // whatever object appears under the finger the instant it becomes
    // visible/clickable, and fire a click for it on the very next release -
    // confirmed by Exit firing immediately after show_menu() with no actual
    // tap. Waiting for release to be genuinely settled avoids that window.
    void poll_release(lv_timer_t *timer)
    {
        if (s_indev && lv_indev_get_state(s_indev) == LV_INDEV_STATE_RELEASED)
        {
            if (++s_release_streak >= 10) // ~500ms at this timer's 50ms period
            {
                show_menu();
                lv_timer_delete(timer);
            }
        }
        else
        {
            s_release_streak = 0;
        }
    }
}

void ui_boot_menu_set_indev(lv_indev_t *indev)
{
    s_indev = indev;
}

void ui_boot_menu_build_pool()
{
    s_overlay = lv_obj_create(lv_layer_top());
    lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_size(s_overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_overlay, LV_OPA_90, 0);
    lv_obj_set_style_pad_all(s_overlay, 24, 0);
    lv_obj_set_flex_flow(s_overlay, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_overlay, 12, 0);
    lv_obj_set_flex_align(s_overlay, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    s_message = lv_label_create(s_overlay);
    lv_label_set_text(s_message, "Release to continue...");
    lv_obj_set_style_text_color(s_message, lv_color_white(), 0);
    lv_obj_set_style_text_align(s_message, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(s_message, LV_PCT(90));

    s_menu_row = lv_obj_create(s_overlay);
    lv_obj_remove_style_all(s_menu_row);
    lv_obj_add_flag(s_menu_row, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_width(s_menu_row, 440);
    lv_obj_set_height(s_menu_row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(s_menu_row, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_menu_row, 10, 0);

    make_menu_btn(s_menu_row, "Restore Defaults", on_restore_defaults_clicked);
    make_menu_btn(s_menu_row, "Erase SD Card", on_format_sd_clicked);
    make_menu_btn(s_menu_row, "Exit", on_exit_clicked);

    s_confirm_row = lv_obj_create(s_overlay);
    lv_obj_remove_style_all(s_confirm_row);
    lv_obj_add_flag(s_confirm_row, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_width(s_confirm_row, 440);
    lv_obj_set_height(s_confirm_row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(s_confirm_row, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_confirm_row, 10, 0);

    s_confirm_btn = lv_button_create(s_confirm_row);
    lv_obj_set_width(s_confirm_btn, LV_PCT(100));
    lv_obj_set_height(s_confirm_btn, 80);
    s_confirm_lbl = lv_label_create(s_confirm_btn);
    lv_label_set_text(s_confirm_lbl, "Confirm");
    lv_obj_set_style_text_font(s_confirm_lbl, &lv_font_montserrat_28, 0);
    lv_obj_center(s_confirm_lbl);
    lv_obj_add_event_cb(s_confirm_btn, on_confirm_clicked, LV_EVENT_CLICKED, nullptr);

    make_menu_btn(s_confirm_row, "Cancel", on_cancel_clicked);
}

void ui_boot_menu_show()
{
    lv_label_set_text(s_message, "Release to continue...");
    lv_obj_clear_flag(s_message, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_menu_row, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_confirm_row, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);

    s_release_streak = 0;
    lv_timer_create(poll_release, 50, nullptr);
}
