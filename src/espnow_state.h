#pragma once

#include <cstdint>

#include "espnow_protocol.h"

// ESP-NOW enablement, messaging, and local device-table bookkeeping for the
// ShackMate protocol (espnow_protocol.h / ESPNOW_PROTOCOL.md).

// Initializes ESP-NOW. Requires Wi-Fi already be in STA (or AP) mode - call
// after app_wifi_apply(). Safe to call once at boot. Returns whether it
// succeeded (also recorded for espnow_is_enabled()).
bool espnow_init();

// True once espnow_init() has succeeded.
bool espnow_is_enabled();

// Registers the broadcast peer, starts listening for other ShackMate
// devices, sends one SM_DISCOVER so anything already on the air announces
// itself immediately, and starts a periodic SM_HEARTBEAT broadcast. Call
// once after espnow_init() succeeds.
void espnow_start_messaging();

// app_wifi_apply() (app_state.cpp) does a full Wi-Fi radio stop/restart
// cycle (WiFi.disconnect(true), then WiFi.mode()) to apply new credentials
// or reconnect - which, on real hardware, silently deinitializes ESP-NOW as
// a side effect (confirmed via a flood of ESP-IDF's own "esp now not
// init!" log errors after this happens). Nothing in the Wi-Fi driver
// notifies this module when that occurs, so app_wifi_apply() calls this
// unconditionally after bringing Wi-Fi back up to defensively restore
// ESP-NOW's initialized state, broadcast peer, and receive callback - all
// of which a deinit wipes out. No-op (besides a harmless re-init attempt)
// if ESP-NOW was never actually knocked out. Safe to call before
// espnow_init() has ever run (e.g. app_wifi_apply()'s first, boot-time
// call) - it only touches state that's meaningful once espnow_init() has
// succeeded at least once.
void espnow_reestablish_after_wifi_change();

// This device's persistent SM_DeviceIdentity.deviceID - derived from the
// Wi-Fi station MAC address (its last 4 bytes), so it's stable across
// reboots without needing anything provisioned or stored.
uint32_t espnow_local_device_id();

// g_config.espnow_friendly_name - editable on the ESP NOW page, persisted to
// NVS. Broadcasts (SM_ANNOUNCE/SM_HEARTBEAT) pick up a changed name on their
// next send with no extra plumbing, since this always reads live from
// g_config rather than a cached copy.
const char *espnow_local_friendly_name();

struct SM_KnownDevice
{
    uint32_t deviceID;
    char friendlyName[24];
    uint8_t deviceType; // SM_DeviceType
    uint32_t lastSeenMs; // millis() timestamp
};

constexpr int kMaxKnownDevices = 16;

// Records (adding or updating) a device as seen just now - called from the
// ESP-NOW receive handler on every SM_ANNOUNCE/SM_HEARTBEAT. If the table
// is full and this is a new deviceID, evicts the least-recently-seen entry.
void espnow_note_device_seen(uint32_t deviceID, const char *friendlyName, uint8_t deviceType);

int espnow_known_device_count();
const SM_KnownDevice *espnow_known_device(int index);

// True if `deviceID` has been seen (SM_ANNOUNCE/SM_HEARTBEAT) within the
// last `maxAgeMs` - a generic mesh-reachability check, for screens that
// need to know whether a specific paired device's link is still alive
// (e.g. ui_fn_main.cpp auto-exiting Simulate mode if the pod goes quiet),
// not just whether it was ever seen at all. False if the device has never
// been seen this boot.
bool espnow_device_seen_within(uint32_t deviceID, uint32_t maxAgeMs);

// Human-readable label for an SM_DeviceType value, for UI display.
const char *espnow_device_type_name(uint8_t deviceType);

// ---- Wi-Fi provisioning ----
//
// This device stays on its normal Wi-Fi channel at all times - it never
// scans or leaves its network. An unprovisioned device (e.g. a
// freshly-booted M5 Atom S3 Lite FN bridge with no saved Wi-Fi credentials)
// sweeps 2.4GHz channels broadcasting SM_PROVISION_REQUEST; this device
// hears it whenever the sweep happens to land on its channel, and
// immediately (no user input needed yet) replies with SM_PROVISION_HOLD so
// the requester stops sweeping and waits. That's what makes
// espnow_has_incoming_pair_request() below become true - the UI is
// expected to then ask the user to confirm before actually provisioning.
// See ESPNOW_PROTOCOL.md's "Wi-Fi provisioning" section for the full
// handshake and its timing rationale.

// A device that has asked to be paired and is currently holding, waiting
// on this device's decision.
struct SM_IncomingPairRequest
{
    uint8_t mac[6];
    uint32_t deviceID;
    char friendlyName[24];
    uint8_t deviceType; // SM_DeviceType
    uint8_t firmwareVersionMajor;
    uint8_t firmwareVersionMinor;
    uint8_t firmwareVersionPatch;
    uint32_t nonce; // echoed back in SM_PROVISION_HOLD/CREDENTIALS/REJECTED - see SM_ProvisionRequest
};

// True if there's a pair request awaiting a user decision. Only one is
// tracked at a time - a second requester's SM_PROVISION_REQUEST arriving
// while one is already pending is ignored until this one is resolved.
bool espnow_has_incoming_pair_request();
const SM_IncomingPairRequest *espnow_incoming_pair_request();

// Accepts the current incoming pair request: sends this device's Wi-Fi
// credentials (g_config.wifi_ssid/wifi_password) to the requester, and
// adds it to the persisted paired-bridges list. Clears the pending
// request and starts tracking for espnow_pair_outcome() below. No-op if
// there isn't one.
void espnow_accept_pair_request();

// Declines the current incoming pair request: tells the requester to
// resume sweeping rather than making it wait out its own hold timeout.
// Clears the pending request. No-op if there isn't one.
void espnow_reject_pair_request();

// Whether the credentials sent by the most recent espnow_accept_pair_request()
// were actually confirmed received (SM_PROVISION_ACK) - sending isn't proof
// of receipt, so this is the real verification that pairing worked.
enum SM_PairOutcome
{
    SM_PAIR_NONE,      // no attempt in progress, or its outcome was already consumed
    SM_PAIR_WAITING,   // credentials sent, waiting on SM_PROVISION_ACK
    SM_PAIR_CONFIRMED, // matching SM_PROVISION_ACK received
    SM_PAIR_TIMED_OUT, // no ACK within the timeout - the pod may not have received it
};

// Call periodically (e.g. from a UI timer) after espnow_accept_pair_request()
// to watch how it resolves - transitions SM_PAIR_WAITING to
// SM_PAIR_TIMED_OUT on its own once the timeout elapses.
SM_PairOutcome espnow_pair_outcome();

// Resets the outcome to SM_PAIR_NONE once the caller has consumed/displayed
// it (e.g. after showing a result dialog), so a stale CONFIRMED/TIMED_OUT
// doesn't linger and get misread as describing a later attempt.
void espnow_clear_pair_outcome();

// A previously-paired device (persisted to NVS, survives reboots).
struct SM_PairedBridge
{
    uint8_t mac[6];
    uint32_t deviceID;
    char friendlyName[24];
    uint8_t deviceType; // SM_DeviceType
};

// Room for more than one physical pod type/instance at once.
constexpr int kMaxPairedBridges = 10;

int espnow_paired_bridge_count();
const SM_PairedBridge *espnow_paired_bridge(int index);

// Removes a paired bridge from the persisted list (e.g. an "Unpair"/
// "Forget" action in the UI). Does not itself notify the bridge - it will
// keep trying to use the network for as long as it holds credentials.
void espnow_forget_paired_bridge(int index);

// Index of the first paired bridge whose deviceType is an FN 2-wire pod
// (SM_DEVICE_FN_2WIRE_POD), or -1 if none is paired yet - shared by the FN
// Output and FN Main screens, which both only ever talk to one such pod.
int espnow_find_fn_pod_bridge_index();

// The most recent SM_PING received (e.g. from a pod's button press), for
// UI display - the ESP NOW page shows "Ping received from <name>". name
// is looked up from the known-devices table by deviceID at the time the
// ping arrived; if that lookup fails (ping arrived before any
// heartbeat/announce from that device), friendlyName falls back to a
// formatted MAC address.
struct SM_LastPing
{
    uint32_t deviceID;
    char friendlyName[24];
    uint32_t receivedMs; // millis() timestamp
};

// nullptr if no ping has been received yet this boot.
const SM_LastPing *espnow_last_ping();

// Most recent FN-pod TX-mode status (SM_STATUS/SM_FnTxStatusPayload,
// broadcast by an FN 2-wire pod's fn_bus_tx whenever it changes - see that
// project's espnow_protocol.h's FnTxMode). Only recorded from a sender
// whose deviceType is SM_DEVICE_FN_2WIRE_POD, so this can't be misread as
// FN status from an unrelated ShackMate device. For the FN Output screen
// to show whether "Outputs Enabled" is really transmitting a per-model FN
// encoding or just a bench placeholder - see ui_fn_output.cpp.
struct SM_FnTxStatus
{
    uint32_t deviceID;
    uint8_t txMode; // FnTxMode
    uint32_t receivedMs; // millis() timestamp
};

// nullptr if no FN pod has reported a TX-mode status yet this boot.
const SM_FnTxStatus *espnow_last_fn_tx_status();

// Most recent FN Main decode snapshot (SM_FN_MAIN_STATUS, broadcast by an
// FN pod whenever its decoded state changes - see espnow_protocol.h's
// SM_FnMainStatusPayload). Only recorded from SM_DEVICE_FN_2WIRE_POD, same
// gating as espnow_last_fn_tx_status(). There is no real FN-MAIN receive
// interface yet - simulating is only ever true from a pod replaying a
// canned capture (see M5AtomS3-FN-Bridge/src/main.cpp), never a live bus.
struct SM_FnMainStatus
{
    uint32_t deviceID;
    bool simulating;
    uint8_t profileMatch;    // FnMainProfileMatch
    bool outputs[16];
    uint8_t analogCode;      // 0-15
    uint8_t lastAddressBits; // packed MSB-first, bit4=A1..bit0=A5
    char captureLabel[24];   // which embedded capture is currently playing
    uint32_t receivedMs;     // millis() timestamp
};

// nullptr if no FN pod has reported an FN Main status yet this boot.
const SM_FnMainStatus *espnow_last_fn_main_status();

// Human-readable label for an SM_MessageType value, for the Monitor page's
// traffic log below (e.g. "PING", "HEARTBEAT").
const char *espnow_message_type_name(uint8_t messageType);

// ---- Traffic log ----
//
// A rolling record of every ESP-NOW message sent or received, for the
// Monitor page. Fixed-size ring buffer - once full, the oldest entry is
// overwritten. Logged generically in send_to()/on_espnow_recv() rather
// than per-message-type, so this always reflects everything on the wire,
// including message types this device doesn't otherwise act on.
struct SM_TrafficLogEntry
{
    bool outgoing;       // true = sent by this device, false = received
    uint8_t messageType; // SM_MessageType
    char peerLabel[24];  // the other side: friendly name if known, else "ALL" (broadcast) or a formatted MAC
    uint32_t timestampMs;
};

constexpr int kMaxTrafficLogEntries = 40;

int espnow_traffic_log_count();

// index 0 is the most recently logged entry, counting backward in time.
const SM_TrafficLogEntry *espnow_traffic_log_entry(int indexFromNewest);

// Broadcasts a one-off SM_PING for manual testing from the Monitor page -
// unlike the pod's button-triggered ping, this device doesn't track/await
// a matching SM_PONG itself; any reply just shows up in the traffic log
// like any other received message.
void espnow_send_test_ping();

// ---- Remote commands ----
//
// Sends SM_COMMAND (unicast) to a paired bridge, for the Diagnostics and FN
// Output pages' per-device actions. `argument` rides along in
// SM_CommandPayload.argument (0 if unused - most command names below don't
// need one). Known command names handled by the FN pod firmware: "REBOOT"
// (restart), "FACTORY_RESET" (forget its saved channel and restart into
// pairing mode - equivalent to its own 5s button hold), "FN_TX_START"/
// "FN_TX_STOP" (Outputs Enabled), "SET_MODEL" (argument: 0 = PCB-110/10
// outputs, 1 = PCB-085/16 outputs + analog), "SET_OUTPUT" (argument: bit 0
// = requested state, bits 1-5 = 0-based output index), "SET_ANALOG"
// (argument: 0-100, percent), "ALL_OUTPUTS_OFF" (argument unused). See
// ui_fn_output.cpp for the full picture - the pod currently only tracks
// this state and ACKs it; it does not yet fold it into the transmitted FN
// waveform (that encoder doesn't exist yet). Fire-and-forget: the pod
// replies SM_ACK if it received it (visible in the traffic log), but
// there's no higher-level retry here.
bool espnow_send_command(int pairedBridgeIndex, const char *commandName, int32_t argument = 0);
