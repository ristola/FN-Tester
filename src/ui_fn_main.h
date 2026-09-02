#pragma once

#include <lvgl.h>

// FN-MAIN Test placeholder screen, reached from the Home tab's "FN Main"
// tile. Eventually: listen to FN-MAIN and report recognizable/valid FN
// frames (see FN_OUTPUT_Tester_Handoff/docs/TESTER_ARCHITECTURE.md).

void ui_fn_main_build_pool();
void ui_fn_main_show();
