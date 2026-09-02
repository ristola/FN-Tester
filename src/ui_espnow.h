#pragma once

#include <lvgl.h>

// ESP NOW page reached from the hamburger drawer: this device's identity
// on the ESP-NOW network (name ID, MAC address, Wi-Fi channel). See
// espnow_protocol.h / ESPNOW_PROTOCOL.md for the message protocol itself -
// actual ESP-NOW send/receive isn't wired up yet, this is identity/status
// only.

void ui_espnow_build_pool();
void ui_espnow_show();
