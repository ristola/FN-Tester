#include "app_state.h"

#include <LittleFS.h>
#include <SD.h>
#include <SPI.h>
#include <WiFi.h>
#include <esp_wifi.h>

#include "espnow_protocol.h"
#include "espnow_state.h"

AppConfig g_config;
bool g_sd_ready = false;
bool g_fs_ready = false;
bool g_wifi_fallback_active = false;

namespace
{
    // How long to give a configured network to actually associate before
    // giving up and pinning a fixed channel instead. Real hardware testing
    // found that without this, an unreachable saved AP leaves Arduino-ESP32
    // auto-reconnecting (and actively channel-scanning) indefinitely in the
    // background - which starves ESP-NOW pairing, since the pod's
    // channel-sweep pairing protocol (M5AtomS3-FN-Bridge/src/main.cpp)
    // needs the CYD parked on one stable channel to be found at all. Roughly
    // matches ui_wifi_setup.cpp's own ~10s interactive connect timeout, with
    // a little slack since this runs unattended at boot.
    constexpr uint32_t kWifiConnectTimeoutMs = 12000;

    // Channel ESP-NOW falls back to when there's no Wi-Fi association.
    // Deliberately SM_PROVISIONING_CHANNEL_MIN (espnow_protocol.h) - that's
    // also the first channel the pod's own sweep dwells on, so a pod
    // pairing right after boot with no Wi-Fi in range finds the CYD on the
    // very first hop instead of needing to sweep further.
    constexpr uint8_t kEspNowFallbackChannel = SM_PROVISIONING_CHANNEL_MIN;

    uint32_t s_wifi_apply_ms = 0;
    bool s_wifi_settled = false; // true once this attempt has connected or fallen back - stops app_wifi_service() from re-checking every tick

    void enter_fallback_channel()
    {
        // Stop the (otherwise indefinite) background connect/scan so it
        // can't keep hopping the radio off the fallback channel.
        WiFi.setAutoReconnect(false);
        WiFi.disconnect(false);
        esp_wifi_set_channel(kEspNowFallbackChannel, WIFI_SECOND_CHAN_NONE);
        g_wifi_fallback_active = true;
        Serial.printf("Wi-Fi: no AP association - ESP-NOW falling back to fixed channel %u\n", kEspNowFallbackChannel);
    }
}

void app_wifi_apply()
{
    // Always bring the radio up in STA mode, even with no saved network -
    // espnow_init()'s esp_now_init() call right after this (see main.cpp)
    // requires the Wi-Fi driver to already be initialized, and otherwise
    // dereferences uninitialized driver state and crashes.
    WiFi.disconnect(true);
    WiFi.mode(WIFI_STA);
    // Arduino-ESP32 defaults STA mode to modem-sleep power saving, which
    // only wakes the radio to receive on the AP's DTIM beacon schedule -
    // fine for a phone checking email, but it delays (or drops) ESP-NOW
    // frames that can arrive at any time, especially from the FN pod
    // which isn't associated to any AP at all. Disabling it keeps the
    // radio continuously listening, which is what makes SM_PING/SM_PONG
    // and remote SM_COMMANDs responsive enough to control real hardware.
    WiFi.setSleep(false);

    // The disconnect(true)+mode() cycle above silently deinitializes
    // ESP-NOW on real hardware whenever this function runs again after
    // boot (Wi-Fi icon reconnect, saving new credentials on the Wi-Fi setup
    // page) - see espnow_reestablish_after_wifi_change()'s comment. No-op
    // before espnow_init() has ever run (this function's own first,
    // boot-time call from main.cpp), so safe to call unconditionally here.
    espnow_reestablish_after_wifi_change();

    // wifi_enabled false means the user explicitly turned Wi-Fi off (top bar
    // icon or the Wi-Fi setup page's toggle) - honor that at boot too,
    // rather than reconnecting anyway just because credentials exist. The
    // radio is still in STA mode above regardless, so ESP-NOW keeps working.
    if (!g_config.wifi_enabled || !g_config.isWifiConfigured())
    {
        // Nothing is ever going to associate - no point waiting out
        // kWifiConnectTimeoutMs, pin the fallback channel immediately.
        s_wifi_settled = true;
        enter_fallback_channel();
        return;
    }

    s_wifi_settled = false;
    g_wifi_fallback_active = false;
    s_wifi_apply_ms = millis();
    WiFi.begin(g_config.wifi_ssid.c_str(), g_config.wifi_password.c_str());
}

void app_wifi_service()
{
    if (s_wifi_settled)
        return;

    if (WiFi.status() == WL_CONNECTED)
    {
        s_wifi_settled = true;
        g_wifi_fallback_active = false;
        return;
    }

    if (millis() - s_wifi_apply_ms >= kWifiConnectTimeoutMs)
    {
        s_wifi_settled = true;
        enter_fallback_channel();
    }
}

void app_wifi_disable()
{
    WiFi.disconnect(true);
    // WiFi.disconnect(true)'s `true` (wifioff) actually powers the radio
    // off, not just dropping the AP association - confirmed on real
    // hardware: calling espnow_reestablish_after_wifi_change() alone left
    // ESP-NOW still unreachable, because esp_now_init() inside it can't
    // revive a powered-off radio. WiFi.mode(WIFI_STA) turns the radio back
    // on first (STA-only, no AP association attempted - exactly what
    // ESP-NOW needs and nothing more).
    WiFi.mode(WIFI_STA);
    espnow_reestablish_after_wifi_change();
    s_wifi_settled = true;
    enter_fallback_channel();

    g_config.wifi_enabled = false;
    config_save(g_config);
}

namespace
{
    // TF_CS/TF_SPI_* come from the board's platformio boards/esp32-8048S043C.json
    // (BOARD_HAS_TF section), so these stay correct if the board target changes.
    SPIClass s_sd_spi(HSPI);
}

bool app_sd_init()
{
#ifdef BOARD_HAS_TF
    s_sd_spi.begin(TF_SPI_SCLK, TF_SPI_MISO, TF_SPI_MOSI, TF_CS);
    g_sd_ready = SD.begin(TF_CS, s_sd_spi);
#else
    g_sd_ready = false;
#endif
    return g_sd_ready;
}

bool app_fs_init()
{
    // formatOnFail=true so a first-ever boot (or a corrupted filesystem)
    // self-heals into an empty, mountable filesystem rather than getting
    // permanently stuck failing LittleFS.open() calls.
    g_fs_ready = LittleFS.begin(/*formatOnFail=*/true);
    return g_fs_ready;
}
