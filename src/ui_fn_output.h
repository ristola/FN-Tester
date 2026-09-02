#pragma once

#include <lvgl.h>

// OUTPUT-board Tester screen, reached from the Home tab's "FN Output" tile.
//
// Lets the operator pick a board profile (PCB-110: 10 outputs, no analog: or
// PCB-085: 16 outputs + one 4-20mA analog channel), enable continuous FN
// transmission to the paired pod, touch-toggle individual outputs on/off,
// and (PCB-085 only) set the 4-20mA analog command as a 0-100% slider.
//
// Per FN_OUTPUT_Tester_Handoff/CLAUDE.md's "Continuous State Requirement":
// this screen issues logical commands ("set output N on", "set analog to
// X%") over ESP-NOW - it does not itself generate FN bus waveforms. The
// pod is responsible for maintaining the full output bitmap and
// continuously transmitting it. As of this screen's introduction the pod
// (M5AtomS3-FN-Bridge) tracks that state and ACKs these commands but does
// NOT yet fold it into the transmitted waveform - it still loops a fixed
// reference frame while "enabled". The real per-model FN frame encoder is
// separate, future work (see that project's AGENTS.md priority list).
//
// GPIO2 on the pod remains bare/unprotected - not wired to a real FN bus
// yet (see FN_OUTPUT_Tester_Handoff/docs/TESTER_ARCHITECTURE.md's
// "Interface Safety" section).

void ui_fn_output_build_pool();
void ui_fn_output_show();
