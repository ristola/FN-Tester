#pragma once

#include <lvgl.h>

// Capture / Learn screen, reached from the Home tab's "Capture / Learn"
// tile. Per FN_OUTPUT_Tester_Handoff/CLAUDE.md's "CAPTURE / LEARN Mode":
// guided support for building/verifying an OUTPUT board's address-to-output
// map, by letting a technician mark what they just did on the real board
// (toggle output N, set the analog command) as a timestamped log entry
// correlated against an external Saleae capture filename.
//
// IMPORTANT: this is a manual observation log, not an automatic capture.
// This tester has no FN-MAIN receive interface yet (see
// FN_OUTPUT_Tester_Handoff/docs/TESTER_ARCHITECTURE.md's development order,
// items 6-7 - "protected ESP32 receive interface" / "passive CYD monitor"
// are both still unbuilt) - it cannot decode or timestamp real FN bus
// traffic on-device. The technician's own Saleae capture remains the actual
// evidence; this screen just helps keep it correlated with what was
// physically done on the board while it was running, and shows the known
// PCB-110/PCB-085 address map as a reference while doing so.

void ui_capture_learn_build_pool();
void ui_capture_learn_show();
