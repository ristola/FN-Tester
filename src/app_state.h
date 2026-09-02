#pragma once

#include "config.h"

// Process-wide current settings (loaded from NVS at boot, updated by the
// Settings screen). Single-threaded access only (see README limitations).
extern AppConfig g_config;

// Disconnects (if needed) and reconnects Wi-Fi using g_config's credentials.
// Safe to call with an empty SSID (no-op). Does nothing towards actually
// joining an AP if g_config.wifi_enabled is false - see app_wifi_disable().
void app_wifi_apply();

// Turns Wi-Fi off and persists that choice (g_config.wifi_enabled = false)
// so app_wifi_apply() won't reconnect on the next boot. The radio itself
// stays in STA mode - ESP-NOW needs that regardless of AP association - only
// the AP connection attempt is skipped. Call app_wifi_apply() (after setting
// wifi_enabled back to true) to turn it back on.
void app_wifi_disable();

// True once the microSD card has been successfully mounted.
extern bool g_sd_ready;

// Initializes the microSD card on the board's dedicated SPI pins. Call once
// from setup(). Returns g_sd_ready.
bool app_sd_init();
