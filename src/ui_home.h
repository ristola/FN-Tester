#pragma once

#include <lvgl.h>

// Builds the Home tab's static content inside `parent` (Wi-Fi status label).
// Call ui_home_start() to begin periodic refresh.
void ui_home_create(lv_obj_t *parent);

// Starts the periodic Wi-Fi status refresh.
void ui_home_start();
