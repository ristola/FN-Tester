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

// Poll from loop() (cheap - just a millis() comparison most ticks). If
// Wi-Fi is enabled+configured but hasn't associated within a boot/reconnect
// grace period, stops the (otherwise indefinite, channel-hopping) connect
// attempt and pins the radio to a fixed fallback channel instead - see
// g_wifi_fallback_active's comment for why. No-op once Wi-Fi has connected
// or fallback has already been entered for the current app_wifi_apply()
// attempt.
void app_wifi_service();

// True when ESP-NOW is running on a fixed fallback channel because there's
// no Wi-Fi association (not configured, disabled, or configured but out of
// range/unreachable) - see app_wifi_service()/app_wifi_apply(). Pairing and
// all other ESP-NOW traffic work normally in this state; only things that
// need an actual AP (OTA, NTP, etc.) don't. False once/if a real AP
// association succeeds.
extern bool g_wifi_fallback_active;

// True once the microSD card has been successfully mounted.
extern bool g_sd_ready;

// Initializes the microSD card on the board's dedicated SPI pins. Call once
// from setup(). Returns g_sd_ready.
bool app_sd_init();

// True once the internal-flash filesystem (LittleFS) has been successfully
// mounted. Independent of g_sd_ready/app_sd_init() above - this bench
// unit's SD card doesn't reliably mount ("Card Failed!" in the boot log),
// so Capture/Learn's board profiles (fn_bank_profile.cpp) and session logs
// (ui_capture_learn.cpp) use LittleFS instead. app_sd_init()/g_sd_ready are
// kept as-is for whatever else still wants the physical SD card (e.g. the
// boot-recovery menu's Erase SD Card action) - this is an addition, not a
// replacement.
extern bool g_fs_ready;

// Mounts LittleFS on the internal flash's "spiffs"-labeled data partition
// (already present in this board's default_16MB.csv partition table - no
// partition table change needed). Call once from setup(). Returns
// g_fs_ready.
bool app_fs_init();
