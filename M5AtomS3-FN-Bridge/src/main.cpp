// M5Stack Atom S3 Lite - FN two-wire "pod" starter firmware.
//
// Scope of this file: join the ShackMate ESP-NOW mesh (discovery +
// heartbeat, matching CYD-4.3-FN-Tester's espnow_state.cpp), learning the
// mesh's radio channel from a ShackMate provisioner (the CYD tester
// automatically offering to pair with any pod it hears) instead of a
// compile-time constant - all visible on hardware via the onboard RGB LED
// + Serial log, since the Lite has no display. It does NOT yet talk to
// the FN two-wire bus itself - that physical interface isn't designed yet
// (see FN_OUTPUT_Tester_Handoff/docs/TESTER_ARCHITECTURE.md's development
// order: symbol/frame format and the protected receive interface come
// before this pod can do anything with real FN traffic).
//
// Deliberately never joins Wi-Fi (no WiFi.begin(), no IP address, no
// SSID/password anywhere): ESP-NOW only requires both radios on the same
// channel, not an actual AP association, so channel number is the only
// thing this pod ever needs to learn. One consequence: no network OTA
// here (that needs a real IP for mDNS/UDP) - reflash over USB.
//
// Channel provisioning: on first boot (or after a factory-reset
// long-press, see kFactoryResetHoldMs), this device doesn't know the
// mesh's channel. It sweeps SM_PROVISIONING_CHANNEL_MIN..MAX, dwelling
// briefly on each one and broadcasting SM_PROVISION_REQUEST, until a
// provisioner - sitting on its own channel the whole time, never having
// to leave it - hears one and replies SM_PROVISION_HOLD. That tells this
// pod to stop sweeping and wait right here, since a human still has to
// confirm the pairing dialog on the provisioner's end, which can take far
// longer than one channel dwell. The provisioner then either sends
// SM_PROVISION_CHANNEL (saved to NVS, ACKed, then reboot to rejoin on
// that channel) or SM_PROVISION_REJECTED (resume sweeping immediately).
// See ../CYD-4.3-FN-Tester/ESPNOW_PROTOCOL.md's "Channel provisioning"
// section for the full exchange, including why every reply echoes a
// nonce.

#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <Preferences.h>
#include <WiFi.h>
#include <cstring>
#include <esp_now.h>
#include <esp_system.h>
#include <esp_wifi.h>

#include "espnow_protocol.h"
#include "fn_bus_tx.h"
#include "fn_pcb085_profile.h"
#include "fn_reference_frame.h"
#include "fn_symbol_codec.h"

namespace
{
    // M5Stack AtomS3 Lite onboard hardware. Confirmed against M5Stack's
    // AtomS3 Lite docs at time of writing - double check if a future
    // hardware revision moves these.
    constexpr uint8_t kRgbLedPin = 35;
    constexpr uint8_t kButtonPin = 41; // active LOW; INPUT_PULLUP below covers it either way

    constexpr uint16_t kFirmwareVersionMajor = 0;
    constexpr uint16_t kFirmwareVersionMinor = 2;
    constexpr uint16_t kFirmwareVersionPatch = 0;
    constexpr const char *kFriendlyName = "FN-POD-01";

    constexpr const char *kPrefsNamespace = "fnbridge";
    constexpr const char *kChannelKey = "channel";
    constexpr const char *kCydMacKey = "cydmac";

    constexpr uint32_t kHeartbeatIntervalMs = 10000;
    constexpr uint32_t kPeerListPrintIntervalMs = 5000;
    // How long to sit on each channel while sweeping, and how often to
    // (re-)broadcast SM_PROVISION_REQUEST during that dwell.
    constexpr uint32_t kChannelDwellMs = 400;
    constexpr uint32_t kProvisionRequestIntervalMs = 2000;
    // How long to wait on a held channel for SM_PROVISION_CHANNEL or
    // SM_PROVISION_REJECTED before giving up and resuming the sweep (the
    // CYD side's own pairing-dialog timeout is a bit shorter than this -
    // see espnow_state.cpp's kPendingRequestTimeoutMs).
    constexpr uint32_t kHoldTimeoutMs = 30000;
    // Hold the front button this long, any time (not just at boot), to
    // forget the saved channel and restart into pairing mode - a physical
    // "restore to defaults" that doesn't require reflashing or even
    // power-cycling.
    constexpr uint32_t kFactoryResetHoldMs = 5000;

    const uint8_t kBroadcastMac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

    // The Lite's onboard indicator is actually 4 chained WS2812 LEDs, not
    // 1 - addressing only 1 pixel leaves the rest of the chain without a
    // full data frame (undefined state, often just off), which can read
    // as "no LED at all" depending on which physical LED ends up lit.
    // Drive all 4 the same color.
    constexpr uint16_t kLedCount = 4;
    Adafruit_NeoPixel s_led(kLedCount, kRgbLedPin, NEO_GRB + NEO_KHZ800);

    uint16_t s_next_sequence = 0;
    uint32_t s_last_heartbeat_ms = 0;
    uint32_t s_last_peer_print_ms = 0;
    bool s_button_was_down = false;
    uint32_t s_button_press_start_ms = 0;
    bool s_factory_reset_fired = false; // guards against re-triggering every loop() once the hold threshold is crossed
    bool s_provisioned = false;         // true once we know the mesh channel and are on the air

    // The specific CYD this pod is paired with - captured from
    // on_provision_channel()'s sender MAC and persisted (survives the
    // reboot-to-rejoin provisioning itself triggers), mirroring the CYD's
    // own SM_PairedBridge.mac (espnow_state.h) that stores this pod's MAC.
    // Knowing each other's MAC is what "paired" actually means on both
    // ends - and forgetting is symmetric: forget_channel() below wipes this
    // alongside the channel (whether triggered by the physical button or a
    // remote FACTORY_RESET), same as the CYD's own Forget removes this
    // pod's MAC from its paired-bridges list.
    uint8_t s_cyd_mac[6] = {};
    bool s_have_cyd_mac = false;
    // Timestamp of the most recent message actually from s_cyd_mac - the
    // LED's red-blink "link lost" cue fires off this. Not a real
    // "connection" (ESP-NOW is connectionless, and this pod still answers/
    // acts on messages from any mesh peer regardless of this) - purely a
    // liveness signal.
    uint32_t s_last_cyd_seen_ms = 0;
    // Comfortably more than 2x the CYD's own heartbeat period (10s - see
    // CYD-4.3-FN-Tester/src/espnow_state.cpp's poll_heartbeat) so a single
    // dropped/collided packet doesn't flip the LED red.
    constexpr uint32_t kCydLinkLostTimeoutMs = 25000;

    // FN_TX_START/FN_TX_STOP are requested here (set from on_command(), which
    // runs on the ESP-NOW receive-callback task) but only ever *executed* in
    // loop() (the main task) - see loop()'s handling below for why: calling
    // fn_bus_tx_start()/stop() directly from the callback task raced against
    // update_led()'s Adafruit_NeoPixel::show() call in loop(), both of which
    // touch the shared RMT peripheral with no locking between them. Confirmed
    // on real hardware as a Guru Meditation IntegerDivideByZero panic inside
    // the RMT driver's clock-divider math, reached from show() - i.e.
    // rmtDeinit() (called from the callback task) was corrupting RMT driver
    // state that the LED's RMT-based show() (called from loop()'s task) read
    // right after, on the *next* loop() iteration. Serializing both onto the
    // same task removes the race instead of guessing at a peripheral-level
    // synchronization fix.
    enum class FnTxRequest
    {
        kNone,
        kStart,
        kStop
    };
    volatile FnTxRequest s_fn_tx_request = FnTxRequest::kNone;

    // Output/analog state requested by the CYD's FN Output screen
    // ("SET_MODEL"/"SET_OUTPUT"/"SET_ANALOG"/"ALL_OUTPUTS_OFF" - see that
    // project's espnow_state.h for the argument encoding). Tracked and
    // ACKed here (plain variable writes, safe directly from the ESP-NOW
    // receive-callback context - unlike s_fn_tx_request above, nothing here
    // touches the shared RMT peripheral). Folded into the transmitted FN
    // waveform by start_fn_tx_for_current_state() below: PCB-085
    // (s_fn_output_model == 1) gets a real per-address encoding via
    // fn_pcb085_profile_build_cycle(); PCB-110 (== 0) has no confirmed
    // address map yet (see FN_OUTPUT_Tester_Handoff/docs/PCB110_ANALYSIS.md)
    // so it still just loops the fixed fn_reference_frame.h capture as an
    // explicitly non-functional placeholder - see FnTxMode/s_fn_tx_mode.
    constexpr int kMaxFnOutputs = 16;
    uint8_t s_fn_output_model = 1; // 0 = PCB-110 (10 outputs), 1 = PCB-085 (16 outputs + analog)
    bool s_fn_output_state[kMaxFnOutputs] = {false};
    uint8_t s_fn_analog_percent = 0;

    // Set from on_command()'s SET_MODEL/SET_OUTPUT/SET_ANALOG/
    // ALL_OUTPUTS_OFF handlers, consumed in loop() - same
    // callback-task-can't-touch-RMT reasoning as s_fn_tx_request above.
    // Rebuilding is a no-op if transmission isn't currently running (the
    // next FN_TX_START will pick up current state anyway).
    volatile bool s_fn_rebuild_requested = false;

    // What's actually being put on GPIO2 right now - see FnTxMode in
    // espnow_protocol.h. Broadcast (SM_STATUS) to the mesh whenever it
    // changes, via set_fn_tx_mode() below, so the CYD's FN Output screen
    // can show whether it's looking at a real encoded cycle or a
    // placeholder rather than just assuming "Outputs Enabled" means the
    // former.
    uint8_t s_fn_tx_mode = FN_TX_MODE_OFF;

    // Scratch buffer for fn_pcb085_profile_build_cycle()'s output - reused
    // across rebuilds rather than allocated per-call (this pod never uses
    // dynamic allocation for anything timing-relevant).
    rmt_data_t s_fn_pcb085_buffer[kFnPcb085MaxWords];

    // TEMPORARY DIAGNOSTIC: a plain digitalWrite() 1Hz square wave on GPIO2,
    // bypassing RMT/fn_bus_tx entirely. Every capture of the RMT-driven
    // reference-frame replay on GPIO2 has come back ~99.5% sub-3.2us noise,
    // consistently, across many attempts, while a GND-to-GND capture and a
    // floating-GPIO1 capture both read perfectly clean - narrowing it down
    // to "something about GPIO2 while actively driven." This isolates
    // whether that's specific to RMT's fast switching, or GPIO2 itself
    // regardless of how it's driven. Serial-only ("square"/"stop"), same
    // reasoning as fn_bus_tx's own bench-test hook - not meant to be a
    // permanent feature.
    constexpr int kSquareTestPin = 2;
    bool s_square_test_active = false;
    bool s_square_test_level = false;
    uint32_t s_square_test_last_toggle_ms = 0;
    constexpr uint32_t kSquareTestHalfPeriodMs = 500; // 1Hz

    // UART decoder test: a few seconds of a recognizable ASCII string over
    // a real UART peripheral (not USB-CDC, which is what `Serial` actually
    // is on this board per ARDUINO_USB_CDC_ON_BOOT=1 - both hardware UARTs
    // are otherwise free) on GPIO2, so the Saleae's built-in Async Serial
    // decoder can confirm end-to-end readability at a glance instead of
    // eyeballing raw pulse timing. Reachable via the CYD's FN Output screen
    // (SM_COMMAND "UART_TEST") or Serial ("uarttest"). Runs for
    // kUartTestDurationMs then auto-stops - shares GPIO2 with fn_bus_tx/the
    // square-wave test, so starting this stops those first (and vice versa).
    HardwareSerial s_uart_test(1); // UART1 - UART0 would fight the console if this board had one, but it doesn't; picked for symmetry/clarity anyway
    constexpr int kUartTestPin = 2;
    // Bumped from an initial 9600 (which decoded perfectly, confirming
    // GPIO2/wire/ground/probe were all clean) to 2Mbaud - deliberately
    // aggressive, putting individual bit periods at 500ns, comparable to
    // the RMT reference frame's shortest real edges (~3.6-7us). If this
    // still decodes cleanly, RMT's own switching (not just "fast edges in
    // general" on this wire) is implicated; if it doesn't, plain edge speed
    // on this bench setup is the whole story.
    constexpr uint32_t kUartTestBaud = 2000000;
    constexpr uint32_t kUartTestDurationMs = 5000;
    bool s_uart_test_active = false;
    uint32_t s_uart_test_stop_at_ms = 0;
    // Set from on_command() (ESP-NOW callback task), consumed in loop() (main
    // task) only - same reasoning as s_fn_tx_request above: uart_test_start()
    // calls fn_bus_tx_stop(), which touches the shared RMT peripheral, so it
    // can't safely run from the callback task either.
    volatile bool s_uart_test_requested = false;

    // Defined further down (needs send_broadcast(), defined later still) -
    // forward-declared so uart_test_start() below (defined earlier in the
    // file than that) can call it.
    void stop_fn_tx();

    void square_test_stop()
    {
        s_square_test_active = false;
    }

    void uart_test_stop()
    {
        if (!s_uart_test_active)
            return;
        s_uart_test.end();
        s_uart_test_active = false;
    }

    void uart_test_start()
    {
        stop_fn_tx();
        square_test_stop();
        s_uart_test.begin(kUartTestBaud, SERIAL_8N1, /*rx=*/-1, kUartTestPin);
        s_uart_test_active = true;
        s_uart_test_stop_at_ms = millis() + kUartTestDurationMs;
        Serial.printf("uarttest: sending on GPIO%d at %u 8N1 for %us - decode it in Saleae's Async Serial analyzer\n",
                      kUartTestPin, kUartTestBaud, kUartTestDurationMs / 1000);
    }

    // Pairing-attempt state, only meaningful while !s_provisioned.
    uint32_t s_pairing_nonce = 0;      // fixed for this whole attempt (regenerated only on a fresh boot into pairing mode)
    uint8_t s_sweep_channel = SM_PROVISIONING_CHANNEL_MIN;
    uint32_t s_last_channel_hop_ms = 0;
    uint32_t s_last_provision_request_ms = 0;
    bool s_holding = false; // parked on one channel, waiting on a provisioner's decision
    uint32_t s_hold_start_ms = 0;
    // Set the instant a real SM_PROVISION_CHANNEL starts being processed -
    // see on_provision_channel()'s comment. Merely clearing s_holding
    // wasn't enough to stop loop()'s sweep logic: it has its own
    // dwell-based hop path (separate from the timeout-triggered one) that
    // fires as soon as s_holding is false and enough time has passed since
    // the last hop - which, by the time a human has actually approved the
    // CYD's dialog, it always has. This flag guards loop()'s entire
    // unprovisioned sweep/hold block instead, so nothing in it can run -
    // silently or otherwise - once a channel is actually being committed.
    bool s_channel_committed = false;

    // Button-triggered connectivity test (only meaningful while
    // s_provisioned - see loop()'s short-press handling).
    constexpr uint32_t kPingTimeoutMs = 3000;
    constexpr uint32_t kPingFlashMs = 150; // how long the LED shows solid white to confirm a PONG arrived
    bool s_ping_awaiting = false;
    uint32_t s_ping_id = 0;
    uint32_t s_ping_sent_ms = 0;
    uint32_t s_ping_flash_until_ms = 0;

    // Brief per-message LED flicker (red = transmitting, green = receiving)
    // as a visual "is this thing even talking" cue without needing Serial -
    // set from send_to()/on_espnow_recv() for every message except
    // SM_HEARTBEAT, which is excluded so its constant 10s drumbeat doesn't
    // drown out the flicker's usefulness for spotting real (ping/command)
    // activity.
    constexpr uint32_t kTrafficFlashMs = 60;
    uint32_t s_tx_flash_until_ms = 0;
    uint32_t s_rx_flash_until_ms = 0;

    struct KnownPeer
    {
        uint32_t deviceID;
        char friendlyName[24];
        uint8_t deviceType;
        uint32_t lastSeenMs;
    };
    constexpr int kMaxKnownPeers = 8;
    KnownPeer s_known_peers[kMaxKnownPeers];
    int s_known_peer_count = 0;

    uint32_t local_device_id()
    {
        uint8_t mac[6];
        WiFi.macAddress(mac);
        return (static_cast<uint32_t>(mac[2]) << 24) | (static_cast<uint32_t>(mac[3]) << 16) |
               (static_cast<uint32_t>(mac[4]) << 8) | static_cast<uint32_t>(mac[5]);
    }

    void fill_local_identity(SM_DeviceIdentity &out)
    {
        out.deviceID = local_device_id();
        out.deviceType = SM_DEVICE_FN_2WIRE_POD;
        out.protocolVersion = 1;
        out.firmwareVersionMajor = kFirmwareVersionMajor;
        out.firmwareVersionMinor = kFirmwareVersionMinor;
        out.firmwareVersionPatch = kFirmwareVersionPatch;
        strncpy(out.friendlyName, kFriendlyName, sizeof(out.friendlyName) - 1);
        out.friendlyName[sizeof(out.friendlyName) - 1] = '\0';
    }

    bool send_to(const uint8_t mac[6], SM_MessageType type, const void *payload, uint16_t payloadLen)
    {
        uint8_t buf[250];
        if (sizeof(SM_Header) + payloadLen > sizeof(buf))
            return false;

        SM_Header header;
        header.version = 1;
        header.messageType = type;
        header.sourceID = local_device_id();
        header.destinationID = 0xFFFFFFFF; // recipient identity isn't tracked by MAC-only messages
        header.sequence = s_next_sequence++;
        header.payloadLength = payloadLen;

        memcpy(buf, &header, sizeof(header));
        if (payloadLen > 0)
            memcpy(buf + sizeof(header), payload, payloadLen);

        bool ok = esp_now_send(mac, buf, sizeof(header) + payloadLen) == ESP_OK;
        if (type != SM_HEARTBEAT)
            s_tx_flash_until_ms = millis() + kTrafficFlashMs;
        return ok;
    }

    bool send_broadcast(SM_MessageType type, const void *payload, uint16_t payloadLen)
    {
        return send_to(kBroadcastMac, type, payload, payloadLen);
    }

    // Broadcasts the current FN TX mode only when it actually changes -
    // see s_fn_tx_mode's declaration. Broadcasting (not unicasting to
    // whichever peer sent the triggering command) matches the reliable
    // path this project already settled on for pairing confirmation.
    void set_fn_tx_mode(uint8_t mode)
    {
        if (s_fn_tx_mode == mode)
            return;
        s_fn_tx_mode = mode;
        SM_FnTxStatusPayload status{mode};
        send_broadcast(SM_STATUS, &status, sizeof(status));
        Serial.printf("-> SM_STATUS fn tx mode = %u\n", mode);
    }

    // (Re)starts GPIO2 transmission from current s_fn_output_model/
    // s_fn_output_state/s_fn_analog_percent. Only ever called from loop()'s
    // task - see s_fn_tx_request's comment for why. PCB-085 gets a real
    // per-address encoding (fn_pcb085_profile.h); PCB-110 has no confirmed
    // address map yet (docs/PCB110_ANALYSIS.md) so it falls back to
    // looping the fixed captured idle frame, clearly marked as a
    // non-functional placeholder via s_fn_tx_mode rather than silently
    // pretending it's real.
    bool start_fn_tx_for_current_state()
    {
        if (s_fn_output_model == 1) // PCB-085
        {
            size_t words = fn_pcb085_profile_build_cycle(s_fn_output_state, s_fn_analog_percent,
                                                           s_fn_pcb085_buffer, kFnPcb085MaxWords);
            if (words == 0)
            {
                Serial.println("fn_pcb085_profile_build_cycle() failed (buffer overflow?) - not transmitting");
                return false;
            }
            if (!fn_bus_tx_start(s_fn_pcb085_buffer, words))
                return false;
            set_fn_tx_mode(FN_TX_MODE_REAL_ENCODED);
            return true;
        }

        // PCB-110 (or any future unrecognized model) - placeholder only.
        if (!fn_bus_tx_start(kFnReferenceFrame, kFnReferenceFrameWords))
            return false;
        set_fn_tx_mode(FN_TX_MODE_PLACEHOLDER);
        return true;
    }

    void stop_fn_tx()
    {
        fn_bus_tx_stop();
        set_fn_tx_mode(FN_TX_MODE_OFF);
    }

    void send_discover()
    {
        send_broadcast(SM_DISCOVER, nullptr, 0);
        Serial.println("-> SM_DISCOVER");
    }

    void send_announce()
    {
        SM_DeviceIdentity identity;
        fill_local_identity(identity);
        send_broadcast(SM_ANNOUNCE, &identity, sizeof(identity));
        Serial.println("-> SM_ANNOUNCE");
    }

    void send_heartbeat()
    {
        SM_Heartbeat hb;
        fill_local_identity(hb.identity);
        hb.uptimeSeconds = millis() / 1000;
        hb.statusFlags = 0;
        send_broadcast(SM_HEARTBEAT, &hb, sizeof(hb));
        Serial.println("-> SM_HEARTBEAT");
    }

    // Button-triggered connectivity test (see loop()'s short-press
    // handling) - broadcasts so any ShackMate device can reply, but in
    // practice on this mesh that's the CYD tester.
    void send_ping()
    {
        s_ping_id = millis();
        s_ping_sent_ms = millis();
        s_ping_awaiting = true;

        SM_PingPayload ping{s_ping_id};
        send_broadcast(SM_PING, &ping, sizeof(ping));
        Serial.printf("-> SM_PING id=0x%08X\n", s_ping_id);
    }

    // Broadcast on whatever channel we're currently sweeping/dwelling on:
    // "I don't know the mesh channel yet, pair me." Carries this attempt's
    // nonce so a later reply can be checked against it.
    void send_provision_request()
    {
        SM_ProvisionRequest req;
        fill_local_identity(req.identity);
        req.nonce = s_pairing_nonce;
        send_broadcast(SM_PROVISION_REQUEST, &req, sizeof(req));
        Serial.printf("-> SM_PROVISION_REQUEST on channel %d, nonce=0x%08X\n", s_sweep_channel, s_pairing_nonce);
    }

    bool load_saved_channel(uint8_t &channelOut)
    {
        Preferences prefs;
        prefs.begin(kPrefsNamespace, /*readOnly=*/true);
        uint8_t channel = prefs.getUChar(kChannelKey, 0);
        prefs.end();
        if (channel < SM_PROVISIONING_CHANNEL_MIN || channel > SM_PROVISIONING_CHANNEL_MAX)
            return false;
        channelOut = channel;
        return true;
    }

    void save_channel(uint8_t channel)
    {
        Preferences prefs;
        prefs.begin(kPrefsNamespace, /*readOnly=*/false);
        prefs.putUChar(kChannelKey, channel);
        prefs.end();
    }

    void forget_channel()
    {
        Preferences prefs;
        prefs.begin(kPrefsNamespace, /*readOnly=*/false);
        prefs.clear(); // also drops kCydMacKey - forgetting means forgetting who the CYD was too
        prefs.end();
    }

    bool load_saved_cyd_mac(uint8_t macOut[6])
    {
        Preferences prefs;
        prefs.begin(kPrefsNamespace, /*readOnly=*/true);
        bool ok = prefs.getBytesLength(kCydMacKey) == 6 && prefs.getBytes(kCydMacKey, macOut, 6) == 6;
        prefs.end();
        return ok;
    }

    void save_cyd_mac(const uint8_t mac[6])
    {
        Preferences prefs;
        prefs.begin(kPrefsNamespace, /*readOnly=*/false);
        prefs.putBytes(kCydMacKey, mac, 6);
        prefs.end();
    }

    void note_peer_seen(uint32_t deviceID, const char *friendlyName, uint8_t deviceType)
    {
        for (int i = 0; i < s_known_peer_count; i++)
        {
            if (s_known_peers[i].deviceID == deviceID)
            {
                strncpy(s_known_peers[i].friendlyName, friendlyName, sizeof(s_known_peers[i].friendlyName) - 1);
                s_known_peers[i].friendlyName[sizeof(s_known_peers[i].friendlyName) - 1] = '\0';
                s_known_peers[i].deviceType = deviceType;
                s_known_peers[i].lastSeenMs = millis();
                return;
            }
        }

        int slot;
        if (s_known_peer_count < kMaxKnownPeers)
        {
            slot = s_known_peer_count;
            s_known_peer_count++;
        }
        else
        {
            slot = 0;
            for (int i = 1; i < kMaxKnownPeers; i++)
                if (s_known_peers[i].lastSeenMs < s_known_peers[slot].lastSeenMs)
                    slot = i;
        }

        s_known_peers[slot].deviceID = deviceID;
        strncpy(s_known_peers[slot].friendlyName, friendlyName, sizeof(s_known_peers[slot].friendlyName) - 1);
        s_known_peers[slot].friendlyName[sizeof(s_known_peers[slot].friendlyName) - 1] = '\0';
        s_known_peers[slot].deviceType = deviceType;
        s_known_peers[slot].lastSeenMs = millis();

        Serial.printf("New ShackMate peer: id=0x%08X type=%u name=\"%s\"\n",
                      deviceID, deviceType, friendlyName);
    }

    void on_provision_hold(const uint8_t *mac, const uint8_t *payload, int payloadLen)
    {
        if (payloadLen < static_cast<int>(sizeof(SM_ProvisionNonce)))
            return;
        SM_ProvisionNonce msg;
        memcpy(&msg, payload, sizeof(msg));
        if (msg.nonce != s_pairing_nonce)
            return; // stale - not for our current attempt

        Serial.printf("<- SM_PROVISION_HOLD from %02X:%02X:%02X:%02X:%02X:%02X - parking here, waiting for a decision\n",
                      mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        s_holding = true;
        s_hold_start_ms = millis();
    }

    void on_provision_rejected(const uint8_t *mac, const uint8_t *payload, int payloadLen)
    {
        if (payloadLen < static_cast<int>(sizeof(SM_ProvisionNonce)))
            return;
        SM_ProvisionNonce msg;
        memcpy(&msg, payload, sizeof(msg));
        if (msg.nonce != s_pairing_nonce)
            return; // stale

        Serial.printf("<- SM_PROVISION_REJECTED from %02X:%02X:%02X:%02X:%02X:%02X - resuming sweep\n",
                      mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        s_holding = false;
        s_last_channel_hop_ms = 0; // hop immediately on the next loop() iteration
    }

    // Ensures `mac` is registered as an ESP-NOW peer before sending to it.
    // Deliberately unencrypted - see espnow_protocol.h's note on why the
    // earlier encrypted-peer design didn't work reliably. `channel`
    // defaults to 0 ("use the interface's current channel" per ESP-IDF's
    // esp_now_add_peer() docs); on_provision_channel() passes the exact
    // channel it just received instead of relying on that auto-resolution.
    void ensure_peer(const uint8_t mac[6], uint8_t channel = 0)
    {
        if (esp_now_is_peer_exist(mac))
            return;

        esp_now_peer_info_t peer = {};
        memcpy(peer.peer_addr, mac, 6);
        peer.channel = channel;
        peer.ifidx = WIFI_IF_STA;
        peer.encrypt = false;
        esp_now_add_peer(&peer);
    }

    // Handles SM_PROVISION_CHANNEL regardless of current state (not just
    // while unprovisioned) - lets a provisioner also re-point an
    // already-provisioned pod at a new channel later, without a separate
    // "forget" step first. The nonce check still applies while
    // unprovisioned (ignore anything not for our current attempt); once
    // already provisioned there is no live attempt/nonce to check against,
    // so any well-formed assignment is accepted.
    void on_provision_channel(const uint8_t *macIn, const uint8_t *payload, int payloadLen)
    {
        if (payloadLen < static_cast<int>(sizeof(SM_ChannelAssignment)))
            return;

        // esp_now_recv_cb_t's mac pointer is only guaranteed valid for the
        // duration of this callback invocation - it points into the WiFi
        // driver's own receive buffer, which can be reused for another
        // incoming packet at any time. This function runs for several
        // hundred ms (save_channel()'s flash write, then a multi-attempt
        // ACK loop with real delay()s between sends, during which other
        // ESP-NOW traffic can absolutely arrive and reuse that buffer), so
        // holding onto the raw pointer and reusing it across that whole
        // span - as this function used to - risked every send after the
        // first firing at a stale/corrupted address instead of the CYD's
        // real MAC. Copy it once, up front, and use only this copy below.
        uint8_t mac[6];
        memcpy(mac, macIn, 6);

        SM_ChannelAssignment assignment;
        memcpy(&assignment, payload, sizeof(assignment));

        if (!s_provisioned && assignment.nonce != s_pairing_nonce)
        {
            Serial.println("<- SM_PROVISION_CHANNEL with a stale/foreign nonce - ignoring");
            return;
        }

        if (assignment.channel < SM_PROVISIONING_CHANNEL_MIN || assignment.channel > SM_PROVISIONING_CHANNEL_MAX)
        {
            Serial.printf("<- SM_PROVISION_CHANNEL with an out-of-range channel %u - ignoring\n", assignment.channel);
            return;
        }

        // This function runs from the ESP-NOW receive-callback context, not
        // loop() - so loop()'s own unprovisioned sweep/hold logic keeps
        // running concurrently the whole time this function is executing.
        // Merely clearing s_holding here isn't enough: loop() has a
        // dwell-based channel-hop path that's independent of the
        // hold-timeout check and fires as soon as s_holding is false *and*
        // enough time has passed since the last hop - which, by the time a
        // human has actually approved the CYD's dialog, it always has (the
        // dwell is only 400ms; a real human decision takes far longer).
        // Confirmed on real hardware: even after clearing s_holding early,
        // the CYD still never received SM_PROVISION_ACK, consistently,
        // every time - this flag closes the gap by stopping loop()'s
        // entire sweep/hold block outright.
        s_holding = false;
        s_channel_committed = true;

        Serial.printf("<- SM_PROVISION_CHANNEL: channel=%u - ACKing, saving, and restarting\n", assignment.channel);

        // Best-effort secondary signal only now - real hardware testing
        // this session found this unicast send never gets a confirmed
        // send_cb delivery from within this specific call context
        // (on_provision_channel(), reached synchronously from the ESP-NOW
        // receive callback for the SM_PROVISION_CHANNEL packet that
        // triggered it), no matter what else got fixed around it (a
        // hold-timeout race, a stale MAC pointer, concurrent heartbeat
        // traffic, flash-write timing on both sides, burst vs. single
        // send) - while every broadcast this device has ever sent, all
        // session, has gotten a confirmed SUCCESS. The CYD's *primary*
        // pairing confirmation now comes from this device's own
        // send_announce() broadcast in enter_joined_mode() instead (see
        // that function's comment) - this ACK is kept only in case it
        // occasionally does get through.
        ensure_peer(mac, assignment.channel);
        SM_ProvisionNonce ack{assignment.nonce};
        for (int attempt = 0; attempt < 4; attempt++)
        {
            send_to(mac, SM_PROVISION_ACK, &ack, sizeof(ack));
            delay(20);
        }

        save_channel(assignment.channel);
        save_cyd_mac(mac);

        delay(100); // let the last ACK actually go out before reset
        ESP.restart();
    }

    // Replies to anyone pinging us (e.g. a future CYD-initiated
    // connectivity check) - not just something we send ourselves.
    void on_ping(const uint8_t *mac, const uint8_t *payload, int payloadLen)
    {
        if (payloadLen < static_cast<int>(sizeof(SM_PingPayload)))
            return;
        SM_PingPayload ping;
        memcpy(&ping, payload, sizeof(ping));
        // A ping can arrive from a device we haven't unicast to yet this
        // boot - esp_now_send() silently fails to an unregistered peer, and
        // a broadcast ping doesn't register one on its own.
        ensure_peer(mac);
        send_to(mac, SM_PONG, &ping, sizeof(ping));
    }

    void on_pong(const uint8_t *payload, int payloadLen)
    {
        if (!s_ping_awaiting || payloadLen < static_cast<int>(sizeof(SM_PingPayload)))
            return;

        SM_PingPayload pong;
        memcpy(&pong, payload, sizeof(pong));
        if (pong.pingID != s_ping_id)
            return; // reply to some earlier/stale ping - not the one we're waiting on

        uint32_t roundTripMs = millis() - s_ping_sent_ms;
        Serial.printf("<- SM_PONG id=0x%08X - round trip %ums\n", pong.pingID, roundTripMs);
        s_ping_awaiting = false;
        s_ping_flash_until_ms = millis() + kPingFlashMs;
    }

    // Remote commands from a provisioner's Diagnostics/FN Output pages.
    // Known names: "REBOOT" (plain restart), "FACTORY_RESET" (forget the
    // saved channel first - equivalent to this pod's own 5s button hold),
    // "FN_TX_START"/"FN_TX_STOP" (start/stop the bench-test GPIO2 waveform
    // replay - see fn_bus_tx.h; same action as the "tx"/"stop" Serial
    // commands in loop() below, just reachable wirelessly from the CYD's FN
    // Output screen now), "UART_TEST" (5s of a Saleae-decodable ASCII UART
    // burst on GPIO2, for confirming the LA/probe setup itself against a
    // known-simple signal - see s_uart_test_requested's comment),
    // "SET_MODEL"/"SET_OUTPUT"/"SET_ANALOG"/"ALL_OUTPUTS_OFF" (the FN
    // Output screen's board-profile/output-bitmap/analog state - see
    // s_fn_output_model's comment for how this is folded into the
    // transmitted waveform via start_fn_tx_for_current_state()).
    // Acknowledges receipt with SM_ACK either way,
    // including for a name this firmware doesn't recognize, so the sender
    // at least knows the packet arrived.
    void on_command(const uint8_t *mac, uint16_t sequence, const uint8_t *payload, int payloadLen)
    {
        if (payloadLen < static_cast<int>(sizeof(SM_CommandPayload)))
            return;

        SM_CommandPayload cmd;
        memcpy(&cmd, payload, sizeof(cmd));
        cmd.commandName[sizeof(cmd.commandName) - 1] = '\0';
        Serial.printf("<- SM_COMMAND \"%s\" from %02X:%02X:%02X:%02X:%02X:%02X\n",
                      cmd.commandName, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

        ensure_peer(mac);
        SM_AckPayload ack{sequence};
        send_to(mac, SM_ACK, &ack, sizeof(ack));

        if (strcmp(cmd.commandName, "REBOOT") == 0)
        {
            delay(150); // let the ACK above actually go out before reset
            ESP.restart();
        }
        else if (strcmp(cmd.commandName, "FACTORY_RESET") == 0)
        {
            // Flip the LED state immediately, before the delay/restart
            // below, rather than leaving it blue for the ~150ms delay plus
            // reboot - a WS2812 holds whatever it was last sent through
            // both. Deliberately does NOT call update_led()/s_led.show()
            // directly here: on_command() runs on the ESP-NOW receive-
            // callback task, and calling anything RMT-based from that task
            // concurrently with loop()'s own update_led() call is exactly
            // what caused a real Guru Meditation panic elsewhere in this
            // file (see s_fn_tx_request's comment) - just set the flag and
            // let loop()'s existing every-iteration update_led() call pick
            // it up, same deferral pattern as FN_TX_START/STOP.
            //
            // s_channel_committed = true alongside it for the same reason
            // on_provision_channel() sets it (see that comment): setting
            // s_provisioned = false alone would make loop()'s own
            // sweep/hold branch start actively hopping channels and
            // broadcasting SM_PROVISION_REQUEST concurrently with this
            // function's own forget_channel()/restart, right up until the
            // reboot actually lands - wasted radio activity in a narrow
            // window this file's own convention says loop() shouldn't
            // touch. s_channel_committed's empty branch keeps loop() inert
            // instead, same as during a real provisioning handoff.
            s_provisioned = false;
            s_channel_committed = true;

            forget_channel();
            delay(150);
            ESP.restart();
        }
        else if (strcmp(cmd.commandName, "FN_TX_START") == 0)
        {
            // Deferred to loop() - see s_fn_tx_request's comment.
            s_fn_tx_request = FnTxRequest::kStart;
        }
        else if (strcmp(cmd.commandName, "FN_TX_STOP") == 0)
        {
            s_fn_tx_request = FnTxRequest::kStop;
        }
        else if (strcmp(cmd.commandName, "UART_TEST") == 0)
        {
            // Deferred to loop() - see s_uart_test_requested's comment.
            s_uart_test_requested = true;
        }
        else if (strcmp(cmd.commandName, "SET_MODEL") == 0)
        {
            s_fn_output_model = static_cast<uint8_t>(cmd.argument);
            s_fn_rebuild_requested = true; // deferred to loop() - see its declaration
            Serial.printf("FN output model -> %u (%s)\n", s_fn_output_model,
                          s_fn_output_model == 0 ? "PCB-110, 10 outputs" : "PCB-085, 16 outputs + analog");
        }
        else if (strcmp(cmd.commandName, "SET_OUTPUT") == 0)
        {
            int index = (cmd.argument >> 1) & 0x1F;
            bool state = (cmd.argument & 1) != 0;
            if (index >= 0 && index < kMaxFnOutputs)
            {
                s_fn_output_state[index] = state;
                s_fn_rebuild_requested = true;
                Serial.printf("FN output %d -> %s\n", index + 1, state ? "ON" : "OFF");
            }
        }
        else if (strcmp(cmd.commandName, "SET_ANALOG") == 0)
        {
            int32_t percent = cmd.argument;
            if (percent < 0)
                percent = 0;
            if (percent > 100)
                percent = 100;
            s_fn_analog_percent = static_cast<uint8_t>(percent);
            s_fn_rebuild_requested = true;
            Serial.printf("FN analog -> %u%%\n", s_fn_analog_percent);
        }
        else if (strcmp(cmd.commandName, "ALL_OUTPUTS_OFF") == 0)
        {
            for (int i = 0; i < kMaxFnOutputs; i++)
                s_fn_output_state[i] = false;
            s_fn_rebuild_requested = true;
            Serial.println("FN outputs -> all OFF");
        }
        else
        {
            Serial.println("Unrecognized command name - ACKed but ignored.");
        }
    }

    void on_espnow_recv(const uint8_t *mac, const uint8_t *data, int len)
    {
        if (len < static_cast<int>(sizeof(SM_Header)))
            return;

        SM_Header header;
        memcpy(&header, data, sizeof(header));
        if (header.version != 1)
            return;
        if (header.sourceID == local_device_id())
            return;

        // Any inbound message specifically from the paired CYD's MAC counts
        // as "link up" - not just SM_ANNOUNCE/SM_HEARTBEAT - so an active
        // session (commands, pings) keeps the LED calm even if a heartbeat
        // happens to land late/collide.
        if (s_have_cyd_mac && memcmp(mac, s_cyd_mac, 6) == 0)
            s_last_cyd_seen_ms = millis();

        const uint8_t *payload = data + sizeof(header);
        int payloadLen = len - static_cast<int>(sizeof(header));

        if (header.messageType != SM_HEARTBEAT)
            s_rx_flash_until_ms = millis() + kTrafficFlashMs;

        switch (header.messageType)
        {
        case SM_ANNOUNCE:
        {
            if (payloadLen < static_cast<int>(sizeof(SM_DeviceIdentity)))
                return;
            SM_DeviceIdentity identity;
            memcpy(&identity, payload, sizeof(identity));
            identity.friendlyName[sizeof(identity.friendlyName) - 1] = '\0';
            note_peer_seen(identity.deviceID, identity.friendlyName, identity.deviceType);
            break;
        }
        case SM_HEARTBEAT:
        {
            if (payloadLen < static_cast<int>(sizeof(SM_Heartbeat)))
                return;
            SM_Heartbeat hb;
            memcpy(&hb, payload, sizeof(hb));
            hb.identity.friendlyName[sizeof(hb.identity.friendlyName) - 1] = '\0';
            note_peer_seen(hb.identity.deviceID, hb.identity.friendlyName, hb.identity.deviceType);
            break;
        }
        case SM_DISCOVER:
            if (s_provisioned)
                send_announce();
            break; // unprovisioned pods don't answer normal-mesh discovery - they're sweeping channels instead
        case SM_PROVISION_HOLD:
            if (!s_provisioned)
                on_provision_hold(mac, payload, payloadLen);
            break;
        case SM_PROVISION_REJECTED:
            if (!s_provisioned)
                on_provision_rejected(mac, payload, payloadLen);
            break;
        case SM_PROVISION_CHANNEL:
            on_provision_channel(mac, payload, payloadLen);
            break;
        case SM_PING:
            on_ping(mac, payload, payloadLen);
            break;
        case SM_PONG:
            on_pong(payload, payloadLen);
            break;
        case SM_COMMAND:
            on_command(mac, header.sequence, payload, payloadLen);
            break;
        default:
            break; // not yet handled (capabilities/values/commands/etc.)
        }
    }

    void set_led(uint32_t color)
    {
        for (uint16_t i = 0; i < kLedCount; i++)
            s_led.setPixelColor(i, color);
        s_led.show();
    }

    // Reflects pairing/mesh state - the only feedback available on
    // hardware with no display. No "connecting" phase anymore: setting
    // the ESP-NOW channel is instant (no AP association to wait on), so
    // this goes straight from "needs pairing" to "on the air." Green vs.
    // blue intentionally maps to "still using the Wi-Fi radio API to
    // search" vs. "settled into steady ESP-NOW-only operation" - this pod
    // never actually joins a Wi-Fi network at any point, so there's no
    // literal "on WiFi" state to show.
    void update_led()
    {
        static const char *s_last_label = ""; // force the first call to actually set+log
        uint32_t color;
        uint32_t altColor = 0; // blink target - off, unless overridden below (red/green alternation)
        const char *label;
        bool blink = false; // true while actively broadcasting SM_PROVISION_REQUEST, as a visual "on the air" cue
        if (!s_provisioned)
        {
            // One unified "not paired yet" cue - alternating red/green -
            // covering both sweeping and holding, so it stays visually
            // obvious the whole time right up until the CYD operator
            // actually taps Accept, instead of shifting from green
            // (sweeping) to cyan (holding), which read as two different,
            // easy-to-miss states. label still distinguishes them in the
            // Serial log even though the LED itself doesn't.
            color = s_led.Color(150, 0, 0);
            altColor = s_led.Color(0, 120, 0);
            blink = true;
            label = s_holding ? "red/green blink (holding, awaiting decision)"
                               : "red/green blink (sweeping, needs pairing)";
        }
        else if (s_have_cyd_mac && millis() - s_last_cyd_seen_ms >= kCydLinkLostTimeoutMs)
        {
            color = s_led.Color(150, 0, 0); // red: joined the mesh, but the paired CYD has gone quiet
            label = "red (mesh joined, CYD link lost)";
            blink = true;
        }
        else
        {
            color = s_led.Color(0, 40, 120); // blue: paired, on the ESP-NOW mesh (steady state)
            label = "blue (on ESP-NOW mesh)";
        }

        // Compared/logged by label rather than color, so the 200ms blink
        // toggle below doesn't spam the log every flicker, but a sweeping
        // <-> holding transition still gets logged even though both now
        // render as the same red/green blink.
        if (strcmp(label, s_last_label) != 0)
        {
            Serial.printf("LED -> %s\n", label);
            s_last_label = label;
        }

        // A pending ping's success flash overrides everything else,
        // regardless of state - a brief, unmistakable "it worked" cue
        // independent of whatever color the mesh/pairing state would
        // otherwise show. Doesn't disturb s_last_color, so normal logging
        // resumes cleanly once the flash ends.
        if (millis() < s_ping_flash_until_ms)
        {
            set_led(s_led.Color(200, 200, 200));
            return;
        }

        // Per-message tx/rx flicker (red/green) - a lower-priority, much
        // briefer cue than the ping-success flash above, just for "is this
        // thing talking at all" visibility without Serial. Checked after
        // the ping flash so a successful ping's white still wins over its
        // own send/receive flicker instead of fighting for the same pixel.
        if (millis() < s_rx_flash_until_ms)
        {
            set_led(s_led.Color(0, 200, 0));
            return;
        }
        if (millis() < s_tx_flash_until_ms)
        {
            set_led(s_led.Color(200, 0, 0));
            return;
        }

        constexpr uint32_t kBlinkPeriodMs = 200;
        bool blinkAlt = blink && (millis() / kBlinkPeriodMs) % 2 == 1;
        set_led(blinkAlt ? altColor : color);
    }

    void print_known_peers()
    {
        Serial.printf("Known ShackMate peers: %d\n", s_known_peer_count);
        uint32_t now = millis();
        for (int i = 0; i < s_known_peer_count; i++)
        {
            const KnownPeer &p = s_known_peers[i];
            Serial.printf("  id=0x%08X type=%u name=\"%s\" last seen %us ago\n",
                          p.deviceID, p.deviceType, p.friendlyName, (now - p.lastSeenMs) / 1000);
        }
    }

    void start_espnow_common()
    {
        if (esp_now_init() != ESP_OK)
        {
            Serial.println("esp_now_init() failed - halting.");
            while (true)
            {
                set_led(s_led.Color(150, 0, 0));
                delay(300);
                set_led(0);
                delay(300);
            }
        }

        esp_now_peer_info_t peer = {};
        memcpy(peer.peer_addr, kBroadcastMac, 6);
        peer.channel = 0; // whatever channel the STA interface is already on
        peer.ifidx = WIFI_IF_STA;
        peer.encrypt = false;
        esp_now_add_peer(&peer);

        esp_now_register_recv_cb(on_espnow_recv);

        Serial.printf("Local ShackMate deviceID: 0x%08X, friendlyName: \"%s\"\n",
                      local_device_id(), kFriendlyName);
    }

    // Doesn't know the mesh channel yet: sweep channels broadcasting
    // SM_PROVISION_REQUEST until a provisioner sends SM_PROVISION_HOLD
    // (see loop() for the sweep/hold state machine), then either
    // SM_PROVISION_CHANNEL (saved, ACKed, reboot) or SM_PROVISION_REJECTED
    // (resume sweeping).
    void enter_pairing_mode()
    {
        s_provisioned = false;
        s_pairing_nonce = esp_random();
        s_sweep_channel = SM_PROVISIONING_CHANNEL_MIN;
        s_holding = false;
        Serial.printf("No saved channel - entering pairing mode, nonce=0x%08X.\n", s_pairing_nonce);

        WiFi.mode(WIFI_STA);
        WiFi.disconnect();
        esp_wifi_set_channel(s_sweep_channel, WIFI_SECOND_CHAN_NONE);

        start_espnow_common();
        update_led();

        send_provision_request();
        s_last_provision_request_ms = millis();
        s_last_channel_hop_ms = millis();
    }

    // Saved channel exists: park the radio on it - no AP association, no
    // IP address, just STA mode tuned to the right channel - and join the
    // ShackMate mesh normally. Instant; there's nothing to wait on.
    void enter_joined_mode(uint8_t channel)
    {
        s_provisioned = true;
        Serial.printf("Joining ESP-NOW mesh on channel %u (no Wi-Fi association)...\n", channel);

        WiFi.mode(WIFI_STA);
        WiFi.disconnect();
        esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);

        update_led();
        start_espnow_common();

        // Modem-sleep power save was tried here and reverted: real hardware
        // testing showed unreliable SM_PING/SM_PONG round trips and missed
        // button-triggered pings once it was enabled, and this pod exists to
        // control real hardware where responsiveness matters far more than
        // an unconfirmed, likely-small battery saving (its benefit is
        // normally tied to an AP's DTIM beacon interval, which doesn't even
        // apply here since this pod never associates with one). Keep the
        // radio fully awake (default power state - no esp_wifi_set_ps call)
        // so it's always listening.

        send_discover();
        // Also announce immediately (not just discover) - the CYD uses this
        // broadcast, not the unicast SM_PROVISION_ACK sent moments ago in
        // on_provision_channel(), to confirm pairing actually completed
        // (see espnow_state.cpp's on_espnow_recv() SM_ANNOUNCE case). Every
        // broadcast this whole device has ever sent has gotten a confirmed
        // send_cb SUCCESS on real hardware, unlike that unicast ACK, so
        // this is the reliable signal - not an additional nice-to-have.
        send_announce();
        s_last_heartbeat_ms = millis();

        s_have_cyd_mac = load_saved_cyd_mac(s_cyd_mac);
        // Grace period: don't show "link lost" before the CYD's first
        // post-boot heartbeat/announce has even had a chance to arrive.
        s_last_cyd_seen_ms = millis();
    }
}

void setup()
{
    Serial.begin(115200);

    pinMode(kButtonPin, INPUT_PULLUP);

    s_led.begin();
    s_led.setBrightness(255); // Color() values above already set the actual intensity

    // Boot self-test flash: bright white, independent of any pairing/mesh
    // logic below, so the LED/wiring/library path can be confirmed working
    // even if something later hangs.
    Serial.println("LED self-test: white flash");
    set_led(s_led.Color(200, 200, 200));
    delay(400);
    set_led(0);
    delay(200);

    update_led();

    uint8_t channel;
    if (load_saved_channel(channel))
        enter_joined_mode(channel);
    else
        enter_pairing_mode();
}

void loop()
{
    uint32_t now = millis();

    // Bench-test-only FN waveform replay. Also reachable wirelessly via
    // SM_COMMAND "FN_TX_START"/"FN_TX_STOP" from the CYD's FN Output screen
    // (see on_command() below) - this Serial "tx"/"stop" path is kept for
    // bench dev without a paired CYD/ESP-NOW link at hand. Meant for
    // verifying the generated GPIO2 waveform against the original capture
    // with a scope/logic analyzer, not for driving real hardware. See
    // fn_bus_tx.h - GPIO2 is bare, unprotected GPIO, not wired to the real
    // FN bus.
    if (Serial.available())
    {
        String cmd = Serial.readStringUntil('\n');
        cmd.trim();
        if (cmd == "tx")
        {
            square_test_stop();
            uart_test_stop();
            start_fn_tx_for_current_state();
        }
        else if (cmd == "stop")
        {
            stop_fn_tx();
            square_test_stop();
            uart_test_stop();
        }
        else if (cmd == "square")
        {
            stop_fn_tx(); // release the pin from RMT first - both can't drive it at once
            uart_test_stop();
            pinMode(kSquareTestPin, OUTPUT);
            s_square_test_level = false;
            digitalWrite(kSquareTestPin, LOW);
            s_square_test_last_toggle_ms = millis();
            s_square_test_active = true;
            Serial.printf("square: toggling GPIO%d at 1Hz (plain digitalWrite, no RMT) - \"stop\" to end\n",
                          kSquareTestPin);
        }
        else if (cmd == "uarttest")
            uart_test_start();
        else if (cmd.length() > 0)
            Serial.printf("Unknown command \"%s\" - try \"tx\", \"square\", \"uarttest\", or \"stop\"\n", cmd.c_str());
    }

    if (s_square_test_active && now - s_square_test_last_toggle_ms >= kSquareTestHalfPeriodMs)
    {
        s_square_test_level = !s_square_test_level;
        digitalWrite(kSquareTestPin, s_square_test_level ? HIGH : LOW);
        s_square_test_last_toggle_ms = now;
    }

    if (s_uart_test_active)
    {
        if (now >= s_uart_test_stop_at_ms)
            uart_test_stop();
        else
            s_uart_test.print("FN TESTER UART DECODER TEST\r\n");
    }

    // Actually perform a UART_TEST requested via SM_COMMAND (see
    // s_uart_test_requested's declaration for why this can't happen inline
    // in on_command()).
    if (s_uart_test_requested)
    {
        s_uart_test_requested = false;
        uart_test_start();
    }

    // Actually perform an FN_TX_START/STOP requested via SM_COMMAND (see
    // s_fn_tx_request's declaration for why this can't just happen inline in
    // on_command()).
    switch (s_fn_tx_request)
    {
    case FnTxRequest::kStart:
        s_fn_tx_request = FnTxRequest::kNone;
        square_test_stop();
        uart_test_stop();
        start_fn_tx_for_current_state();
        break;
    case FnTxRequest::kStop:
        s_fn_tx_request = FnTxRequest::kNone;
        stop_fn_tx();
        break;
    case FnTxRequest::kNone:
        break;
    }

    // Re-encode and restart with fresh state after a SET_MODEL/SET_OUTPUT/
    // SET_ANALOG/ALL_OUTPUTS_OFF command - see s_fn_rebuild_requested's
    // declaration. No-op if transmission isn't currently running.
    if (s_fn_rebuild_requested)
    {
        s_fn_rebuild_requested = false;
        if (fn_bus_tx_is_running())
            start_fn_tx_for_current_state();
    }

    // Front button: hold for kFactoryResetHoldMs (5s) *any time*, not just
    // at boot, to forget the saved channel and restart into pairing mode -
    // a physical "restore to defaults" without reflashing or
    // power-cycling. A short press instead (release before the threshold)
    // sends an SM_PING while provisioned, as a manual connectivity test -
    // the LED flashes white when the CYD's SM_PONG comes back (see
    // update_led()).
    bool button_down = (digitalRead(kButtonPin) == LOW);
    if (button_down && !s_button_was_down)
    {
        s_button_press_start_ms = now;
        s_factory_reset_fired = false;
    }
    if (button_down && !s_factory_reset_fired && now - s_button_press_start_ms >= kFactoryResetHoldMs)
    {
        s_factory_reset_fired = true; // guard: don't re-trigger every remaining loop() while still held
        Serial.println("Button held 5s - restoring factory defaults.");
        for (int i = 0; i < 6; i++)
        {
            set_led(s_led.Color(150, 0, 0));
            delay(80);
            set_led(0);
            delay(80);
        }
        forget_channel();
        delay(100);
        ESP.restart();
    }
    if (!button_down && s_button_was_down && !s_factory_reset_fired)
    {
        constexpr uint32_t kMinClickMs = 30; // ignore sub-30ms contact bounce as a spurious click
        uint32_t held = now - s_button_press_start_ms;
        if (held >= kMinClickMs && s_provisioned)
        {
            Serial.println("Button pressed - sending SM_PING to test connectivity");
            send_ping();
        }
    }
    s_button_was_down = button_down;

    // s_channel_committed gets its own branch, doing nothing at all - not
    // folded into either the sweep/hold or the heartbeat/ping-timeout
    // logic below. This used to be `if (!s_provisioned && !s_channel_committed)
    // {sweep} else {heartbeat/etc.}`, but while a channel is being
    // committed, s_provisioned is *still false* (it only flips true on the
    // next boot's enter_joined_mode()) - so that fell through to the
    // heartbeat/etc. branch instead of doing nothing. Confirmed on real
    // hardware: an extra concurrent send_heartbeat() broadcast fired mid
    // ACK-burst and one of the ACK attempts outright failed right after -
    // almost certainly that extra traffic contending for ESP-NOW's small
    // internal send queue with the ACKs on_provision_channel() (running
    // concurrently, from the receive-callback context) is still trying to
    // get out. Nothing in loop() should touch the radio at all during this
    // narrow window - on_provision_channel() has full control until reboot.
    if (s_channel_committed)
    {
        // Deliberately empty.
    }
    else if (!s_provisioned)
    {
        if (s_holding)
        {
            if (now - s_hold_start_ms >= kHoldTimeoutMs)
            {
                Serial.println("Hold timed out with no decision - resuming sweep.");
                s_holding = false;
                s_last_channel_hop_ms = 0; // hop immediately below
            }
        }

        if (!s_holding)
        {
            if (now - s_last_channel_hop_ms >= kChannelDwellMs)
            {
                s_sweep_channel++;
                if (s_sweep_channel > SM_PROVISIONING_CHANNEL_MAX)
                    s_sweep_channel = SM_PROVISIONING_CHANNEL_MIN;
                esp_wifi_set_channel(s_sweep_channel, WIFI_SECOND_CHAN_NONE);
                s_last_channel_hop_ms = now;
                send_provision_request();
                s_last_provision_request_ms = now;
            }
            else if (now - s_last_provision_request_ms >= kProvisionRequestIntervalMs)
            {
                send_provision_request();
                s_last_provision_request_ms = now;
            }
        }
    }
    else
    {
        if (now - s_last_heartbeat_ms >= kHeartbeatIntervalMs)
        {
            send_heartbeat();
            s_last_heartbeat_ms = now;
        }

        if (now - s_last_peer_print_ms >= kPeerListPrintIntervalMs)
        {
            print_known_peers();
            s_last_peer_print_ms = now;
        }

        if (s_ping_awaiting && now - s_ping_sent_ms >= kPingTimeoutMs)
        {
            Serial.printf("Ping id=0x%08X timed out - no SM_PONG\n", s_ping_id);
            s_ping_awaiting = false;
        }
    }

    update_led();
    delay(20);
}
