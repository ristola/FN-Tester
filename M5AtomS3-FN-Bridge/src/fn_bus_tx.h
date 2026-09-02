#pragma once

#include <cstddef>
#include <cstdint>

#include "esp32-hal-rmt.h"

// Bench-test-only FN two-wire waveform transmit, driven by the ESP32-S3's
// RMT peripheral for precise (1us-tick) hardware timing that plain
// digitalWrite() bit-banging can't guarantee under interrupt/WiFi/ESP-NOW
// load.
//
// Outputs on GPIO2 (the Grove port's G1 pin - the only exposed, currently
// unused pin on this board - see README.md's pinout table). This is BARE
// GPIO: DO NOT connect it to the real FN two-wire bus. No isolation, level
// shifting, or protection circuitry exists yet for this pod (see the CYD
// project's FN_OUTPUT_Tester_Handoff/docs/TESTER_ARCHITECTURE.md's
// "Interface Safety" section). This exists purely so a generated waveform
// can be bench-verified with a scope/logic analyzer before any real
// hardware is ever touched.
//
// This module is a generic hardware-loop RMT transmitter - it has no idea
// what the buffer it's given means (a fixed captured reference frame, a
// live per-model FN encoding, whatever) - see main.cpp for what actually
// gets built and passed in, and fn_pcb085_profile.h/fn_symbol_codec.h for
// the real encoder.

// ESP32-S3 has only 4 TX RMT channels x 64 words each = 256 words max for
// one channel object (confirmed on real hardware: requesting more fails
// outright with "not enough channels", regardless of anything else using
// RMT). True hardware auto-loop mode (rmtLoop(), used here) requires the
// whole pattern to fit in that reserved memory.
constexpr size_t kFnBusTxMaxWords = 256;

// Starts (or restarts) looping `frame` (length `wordCount`, must be <=
// kFnBusTxMaxWords) on GPIO2. Copies nothing - `frame` must remain valid
// only for the duration of this call (rmtLoop() takes its own copy
// internally). Returns false if wordCount is out of range or the RMT
// peripheral failed to initialize (logged to Serial either way).
bool fn_bus_tx_start(const rmt_data_t *frame, size_t wordCount);

// Stops transmission and releases the RMT channel/pin. Safe to call even if
// not currently running.
void fn_bus_tx_stop();

// True if a loop is currently active.
bool fn_bus_tx_is_running();
