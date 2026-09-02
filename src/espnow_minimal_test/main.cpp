// Minimal isolated ESP-NOW test - no LVGL, no LovyanGFX, no touch, no SD,
// no OTA. Just WiFi STA + ESP-NOW + a fast broadcast loop, to determine
// whether the ESP_ERR_ESPNOW_NO_MEM lockup seen in the full CYD-4.3-FN-Tester
// app reproduces on bare board+core, or whether it's specific to something
// in the full application (display driver, touch driver, SD, OTA, etc).
#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

#include "../espnow_protocol.h"

namespace
{
    const uint8_t kBroadcastMac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    uint32_t s_seq = 0;
    uint32_t s_ok_count = 0;
    uint32_t s_fail_count = 0;

    void on_send(const uint8_t *mac, esp_now_send_status_t status)
    {
        // Diagnostic only - mirrors the full app's on_espnow_send() callback.
        (void)mac;
        (void)status;
    }
}

void setup()
{
    Serial.begin(115200);
    delay(200);
    Serial.println("\n=== minimal espnow test boot ===");

    WiFi.disconnect(true);
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    esp_wifi_set_ps(WIFI_PS_NONE);

    esp_err_t initErr = esp_now_init();
    Serial.printf("esp_now_init() -> %s\n", esp_err_to_name(initErr));

    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, kBroadcastMac, 6);
    peer.channel = 0;
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = false;
    esp_err_t addErr = esp_now_add_peer(&peer);
    Serial.printf("esp_now_add_peer(broadcast) -> %s\n", esp_err_to_name(addErr));

    esp_now_register_send_cb(on_send);

    Serial.printf("WiFi channel: %d, free heap: %u, free DMA heap: %u\n",
                  WiFi.channel(), ESP.getFreeHeap(),
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA));
}

void loop()
{
    static uint32_t lastSend = 0;
    uint32_t now = millis();
    // Fire every 250ms (much faster than the app's 10s heartbeat) to reach
    // the failure point quickly and get a clean send-count-before-failure
    // number without waiting minutes per test.
    if (now - lastSend >= 250)
    {
        lastSend = now;
        s_seq++;

        // A real, protocol-valid SM_ANNOUNCE so the pod's already-running
        // bridge firmware (which validates SM_Header.version and ignores
        // anything malformed) will actually recognize and log this,
        // unlike raw test bytes.
        uint8_t buf[sizeof(SM_Header) + sizeof(SM_DeviceIdentity)];
        SM_Header header{};
        header.version = 1;
        header.messageType = SM_ANNOUNCE;
        header.sourceID = 0xABCD1234; // arbitrary, just needs to not match the pod's own ID
        header.destinationID = 0xFFFFFFFF;
        header.sequence = static_cast<uint16_t>(s_seq);
        header.payloadLength = sizeof(SM_DeviceIdentity);
        SM_DeviceIdentity identity{};
        identity.deviceID = 0xABCD1234;
        identity.deviceType = SM_DEVICE_GENERIC;
        identity.protocolVersion = 1;
        strncpy(identity.friendlyName, "MINIMAL-TEST", sizeof(identity.friendlyName) - 1);
        memcpy(buf, &header, sizeof(header));
        memcpy(buf + sizeof(header), &identity, sizeof(identity));

        esp_err_t err = esp_now_send(kBroadcastMac, buf, sizeof(buf));
        if (err == ESP_OK)
            s_ok_count++;
        else
            s_fail_count++;

        Serial.printf("seq=%lu esp_now_send() -> %s (ok=%lu fail=%lu, free heap=%u, free DMA=%u, ch=%d)\n",
                      (unsigned long)s_seq, esp_err_to_name(err),
                      (unsigned long)s_ok_count, (unsigned long)s_fail_count,
                      ESP.getFreeHeap(), (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA),
                      WiFi.channel());
    }
}
