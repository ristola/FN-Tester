#pragma once

#include <Arduino.h>

// Persisted (NVS) settings: Wi-Fi credentials and app settings.
struct AppConfig
{
    static constexpr int kMaxSavedNetworks = 8;

    // Previously-joined networks, keyed by SSID, so re-selecting one from a
    // scan (e.g. after moving to a different AP) can join it directly
    // without re-entering the password.
    String saved_ssids[kMaxSavedNetworks];
    String saved_passwords[kMaxSavedNetworks];
    int saved_network_count = 0;

    String wifi_ssid;         // currently-active network, applied at boot
    String wifi_password;

    // Whether app_wifi_apply() should actually call WiFi.begin() at boot (or
    // after a manual toggle). false means "user turned Wi-Fi off" - the
    // radio still comes up in STA mode either way (ESP-NOW needs that
    // regardless of AP association - see app_wifi_apply()'s own comment),
    // this only gates the AP connection attempt itself. Persisted so a
    // reboot doesn't silently start reconnecting after the user turned it
    // off.
    bool wifi_enabled = true;

    // Backlight screen saver: after this many seconds with no touch input,
    // the backlight is turned off (any touch turns it back on). Timeout is
    // only meaningful while enabled is true.
    bool screensaver_enabled = true;
    int screensaver_timeout_sec = 60;

    // ESP-NOW ShackMate friendly name, editable on the ESP NOW page.
    // SM_DeviceIdentity.friendlyName is a fixed char[24] (23 chars + NUL) -
    // kept clamped to that length wherever it's set.
    String espnow_friendly_name = "FN Tester - 01";

    // OUTPUT-board model selected on the FN Output page - 0 = PCB-110 (8
    // outputs, no analog), 1 = PCB-085 (16 outputs + 4-20mA analog). See
    // ui_fn_output.cpp's FnOutputModel. Persisted so the operator doesn't
    // have to re-pick it every boot.
    uint8_t fn_output_model = 1;

    bool isWifiConfigured() const { return wifi_ssid.length() > 0; }

    // Looks up a previously-saved password for `ssid`. Returns false (and
    // leaves passwordOut untouched) if this SSID was never saved.
    bool findSavedPassword(const String &ssid, String &passwordOut) const;

    // Adds or updates (by SSID) a saved network. If the table is full and
    // `ssid` isn't already in it, the oldest entry is evicted.
    void rememberNetwork(const String &ssid, const String &password);
};

// Loads settings from NVS into `out`. Missing keys default to empty strings.
void config_load(AppConfig &out);

// Persists `in` to NVS.
void config_save(const AppConfig &in);

// Wipes all persisted settings (Wi-Fi, saved networks, etc.) from NVS.
// Does not touch `out` in memory - call config_load() afterwards
// (typically after a reboot) to pick up the now-empty defaults.
void config_clear();
