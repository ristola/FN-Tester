#pragma once

#include <lvgl.h>

// Full-screen splash (logo + connection status) shown for a couple of
// seconds at boot while Wi-Fi comes up, on top of everything else via
// lv_layer_top(). Doesn't block anything - Wi-Fi.begin() is already
// asynchronous, so this just gives the user something to look at while it
// happens instead of a stretch of black screen.

// Builds the (visible-by-default) overlay content. Call once alongside the
// other *_build_pool() calls, after ui_boot_menu_build_pool() so it draws on
// top of that (also on lv_layer_top(), but hidden until triggered).
void ui_boot_splash_build_pool();

// Starts the fixed display window. Call once from setup(), after
// app_wifi_apply().
void ui_boot_splash_start();

// Hides the splash immediately - call this if the boot-recovery-menu touch
// fires, so that overlay is visible instead of sitting behind the splash.
void ui_boot_splash_hide();
