#pragma once

#include <lvgl.h>

// Boot-time recovery menu: reset the board, then tap and hold the screen
// within the first ~8s of boot (see main.cpp's s_boot_check_active window)
// to open a small menu with maintenance options (Restore Defaults, Erase SD
// Card, Exit). Must be told which indev to watch so it can require the
// triggering touch to be released before any option can actually be
// selected - otherwise whatever button ends up under the still-down finger
// would fire immediately.
//
// The touch has to be a fresh press *after* boot, not a press held through
// reset: the GT911 calibrates its own "no touch" baseline against whatever
// it sees during its own init, so a finger already on the glass at that
// point becomes invisible to it until lifted and placed again - confirmed
// on this board's hardware, not something fixable in our own code.

// Records the touch indev to watch for the release-gating below. Call once
// right after lv_indev_create().
void ui_boot_menu_set_indev(lv_indev_t *indev);

// Builds the (hidden) overlay content. Call once after the first successful
// render, alongside the other *_build_pool() calls.
void ui_boot_menu_build_pool();

// Shows the overlay - call this only if the screen was already being held
// down at boot. Buttons stay inert until the touch is released at least
// once; an Exit button is always available to back out without doing
// anything.
void ui_boot_menu_show();
