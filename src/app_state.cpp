#include "app_state.h"

#include <SD.h>
#include <SPI.h>
#include <WiFi.h>

#include "espnow_state.h"

AppConfig g_config;
bool g_sd_ready = false;

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
        return;

    WiFi.begin(g_config.wifi_ssid.c_str(), g_config.wifi_password.c_str());
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
