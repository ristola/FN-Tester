#pragma once

#include <lvgl.h>

// Wi-Fi setup screen, reached from the hamburger drawer menu. A full-screen
// overlay on lv_layer_top(), like the other drawer destinations - see
// ui_shell.h. Lists scanned networks with a checkmark on the currently
// joined one; tapping a previously-saved network joins it immediately,
// tapping a new one switches to a focused password-entry view (SSID +
// password field + keyboard only, nothing else) instead of a shared form.

// Pre-creates the (hidden) overlay content. Call once after the first
// successful render, alongside the other *_build_pool() calls.
void ui_wifi_setup_build_pool();

// Shows the overlay and kicks off a fresh scan.
void ui_wifi_setup_show();
