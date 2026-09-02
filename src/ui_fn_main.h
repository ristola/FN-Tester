#pragma once

#include <lvgl.h>

// FN-MAIN listener screen, reached from the Home tab's "FN Main" tile.
// There is no protected/isolated FN-MAIN receive interface on the pod yet
// (see FN_OUTPUT_Tester_Handoff/docs/TESTER_ARCHITECTURE.md's "Interface
// Safety" section), so this can't listen to a real bus - instead it drives
// a "Simulate" toggle that arms the pod's front button to replay a real,
// evidence-backed capture through the pod's real-time decoder
// (M5AtomS3-FN-Bridge/src/fn_word_decoder.h), so the decode+profile
// pipeline and this LED display can be proven and demonstrated without any
// receive hardware existing.

void ui_fn_main_build_pool();
void ui_fn_main_show();
