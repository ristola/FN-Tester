#pragma once

#include <lvgl.h>

// Top status bar (hamburger menu icon, centered title, ESP-NOW icon, Wi-Fi
// icon) sitting above the tabview, plus the drawer menu it opens (Wi-Fi,
// ESP-NOW, Setup). The drawer and the screens it opens are full-screen
// overlays on lv_layer_top(), so the tabview underneath is never torn
// down/rebuilt when navigating away from it - see ui_wifi_setup.h,
// ui_espnow.h, ui_setup.h.

// Builds the top bar as a child of `parent` (the root screen). Create this
// before the tabview, which should be `parent`'s other child, sized to fill
// the remaining space below this bar.
void ui_shell_create(lv_obj_t *parent);

// Pre-creates the (hidden) drawer overlay. Call once after the first
// successful render, alongside the other *_build_pool() calls (and after
// the drawer destinations' own build_pool() calls, since opening one of them
// is wired up here).
void ui_shell_build_pool();

// Replaces the centered title with a fixed string (e.g. while a full-page
// overlay like Setup/ESP-NOW is open, or a tab wants to show live status
// there instead). Call ui_shell_clear_status_override() to restore the
// default app title.
void ui_shell_set_status_override(const char *text);

// Restores the default app title in the top bar's centered label.
void ui_shell_clear_status_override();

// The centered title label itself - for callers that need to attach their
// own event (e.g. a tap handler) rather than just set its text.
lv_obj_t *ui_shell_get_title_label();
