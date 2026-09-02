#pragma once

#include <cstdint>

// ShackMate ESP-NOW device discovery and control protocol - shared wire
// format for every ShackMate ESP32 device (this CYD panel, rotors, power
// controllers, SWR meters, weather stations, display/button panels, etc).
// See ESPNOW_PROTOCOL.md at the project root for the full description.
// Send/receive isn't wired up yet (see espnow_state.h) - this file is the
// data model only.
//
// ESP-NOW payloads are capped at 250 bytes, which every struct here is
// sized to respect on its own - but a device with many capabilities (a
// rotor might advertise 10 values + 6 commands) won't fit them all in one
// SM_CAPABILITIES packet. That's why SM_CapabilitiesPayload carries a
// fragment index/count: the sender splits its full capability list across
// as many packets as needed, and a receiver reassembles by deviceID.

enum SM_MessageType : uint8_t
{
    // Discovery
    SM_DISCOVER = 0x01,             // broadcast: "who's out there?"
    SM_ANNOUNCE = 0x02,             // broadcast/reply: SM_DeviceIdentity
    SM_HEARTBEAT = 0x03,            // periodic broadcast: SM_Heartbeat
    SM_CAPABILITIES = 0x04,         // reply: SM_CapabilitiesPayload (possibly fragmented)
    SM_CAPABILITIES_REQUEST = 0x05, // unicast: "send me your capabilities"

    // ESP-NOW channel provisioning - see "Channel provisioning" in
    // ESPNOW_PROTOCOL.md for the full handshake, and why SM_PROVISION_HOLD
    // and the nonce exist. Note there's no Wi-Fi joining involved anywhere
    // here: ESP-NOW only needs both sides on the same radio channel, not
    // an actual AP association/IP address, so that's all this hands over.
    SM_PROVISION_REQUEST = 0x06,  // broadcast, while channel-sweeping: SM_ProvisionRequest - "I don't know the mesh channel yet, pair me"
    SM_PROVISION_HOLD = 0x07,     // unicast, provisioner -> requester: SM_ProvisionNonce - "I see you - stop sweeping and wait on this channel"
    SM_PROVISION_CHANNEL = 0x08,  // unicast: SM_ChannelAssignment - "here's the channel to use"
    SM_PROVISION_REJECTED = 0x09, // unicast, provisioner -> requester: SM_ProvisionNonce - "declined - resume sweeping"
    SM_PROVISION_ACK = 0x0A,      // unicast, requester -> provisioner: SM_ProvisionNonce - "channel saved, rejoining"

    // Data
    SM_STATUS = 0x10,      // general status payload (device-specific - see SM_FnTxStatusPayload for the FN pod's use of it)
    SM_VALUE = 0x11,       // SM_ValuePayload - one named value's current reading
    SM_SUBSCRIBE = 0x12,   // SM_SubscribeRequest - "push me updates for this value"
    SM_UNSUBSCRIBE = 0x13, // SM_SubscribeRequest (rate field ignored)
    SM_PING = 0x14,        // broadcast or unicast: SM_PingPayload - "are you there?" (round-trip connectivity check)
    SM_PONG = 0x15,        // reply, unicast to the pinger: SM_PingPayload with the same pingID echoed back

    // Commands
    SM_SET_VALUE = 0x20, // SM_ValuePayload - set a writable value
    SM_COMMAND = 0x21,   // SM_CommandPayload - trigger a named action
    SM_RENAME = 0x22,    // unicast: SM_RenamePayload - "call yourself this from now on" (persisted, not a one-shot label)
    SM_FN_MAIN_STATUS = 0x23, // broadcast: SM_FnMainStatusPayload - the FN pod's live/simulated FN-MAIN decode snapshot

    // Responses
    SM_ACK = 0x30,   // SM_AckPayload - acknowledges a message by sequence #
    SM_ERROR = 0x31, // SM_ErrorPayload - reports a failure
};

// What kind of ShackMate device this is. Append-only: never renumber or
// reuse a value once devices with the old numbering exist in the field.
enum SM_DeviceType : uint8_t
{
    SM_DEVICE_GENERIC = 0,
    SM_DEVICE_C64_ULTIMATE_GATEWAY = 1, // e.g. the C64-CYD companion panel
    SM_DEVICE_ANTENNA_ROTOR = 2,
    SM_DEVICE_AUX_POWER_CONTROLLER = 3,
    SM_DEVICE_SWR_METER = 4,
    SM_DEVICE_WEATHER_STATION = 5,
    SM_DEVICE_MESHTASTIC_INTERFACE = 6,
    SM_DEVICE_STATUS_DISPLAY = 7,
    SM_DEVICE_BUTTON_PANEL = 8,
    SM_DEVICE_FN_2WIRE_POD = 9, // M5 Atom S3 Lite: physical interface ("pod") to the FN two-wire bus
};

// An unprovisioned device sweeps this channel range, dwelling briefly on
// each one (see the FN pod's kChannelDwellMs) and broadcasting
// SM_PROVISION_REQUEST, until a provisioner - already sitting on its own
// Wi-Fi network's channel, never having to leave it - hears the request
// and replies. 1-11 covers the 2.4GHz channels legal in every regulatory
// domain (some allow up to 13/14; 11 is the conservative common subset).
constexpr uint8_t SM_PROVISIONING_CHANNEL_MIN = 1;
constexpr uint8_t SM_PROVISIONING_CHANNEL_MAX = 11;

// NOTE: the provisioning handshake was originally designed to encrypt
// SM_PROVISION_CHANNEL/SM_PROVISION_ACK using ESP-NOW's built-in per-peer
// AES support (a shared compile-time PMK/LMK). In practice this didn't
// work reliably: encryption requires *both* sides to have already
// registered each other as an encrypted peer before the first encrypted
// packet arrives, but the requester has no way to do that in advance for
// a provisioner it's never talked to yet. Reverted to plain (unencrypted)
// messages for reliability - see ESPNOW_PROTOCOL.md's "Channel
// provisioning" section. Revisit with a real handshake (e.g. an
// unencrypted key exchange first) if this ever needs to be more than a
// bench-setup convenience. That said, this handshake also no longer
// carries any real secret (a channel number, not Wi-Fi credentials), so
// there's less to protect than there used to be.

// Data type of a single capability's value. Append-only, same as above.
enum SM_ValueType : uint8_t
{
    SM_VAL_BOOL = 0,
    SM_VAL_INT32 = 1,
    SM_VAL_UINT32 = 2,
    SM_VAL_FLOAT32 = 3,
    SM_VAL_COMMAND = 4, // no value - this capability is a command, not a readable/writable value
};

// Physical unit (or lack thereof) a capability's value is expressed in.
// SM_UNIT_ENUM means the value is one of a small fixed set of integers
// specific to that capability (e.g. Direction: 0=CW, 1=CCW), not a
// physical quantity - documented per-device, not by this protocol.
enum SM_Unit : uint8_t
{
    SM_UNIT_NONE = 0,
    SM_UNIT_DEGREES = 1,
    SM_UNIT_METERS = 2,
    SM_UNIT_METERS_PER_SEC = 3,
    SM_UNIT_VOLTS = 4,
    SM_UNIT_AMPS = 5,
    SM_UNIT_WATTS = 6,
    SM_UNIT_CELSIUS = 7,
    SM_UNIT_PERCENT = 8,
    SM_UNIT_HZ = 9,
    SM_UNIT_DBM = 10,
    SM_UNIT_ENUM = 11,
    SM_UNIT_BOOLEAN = 12,
};

enum SM_CapabilityFlags : uint8_t
{
    SM_CAP_READABLE = 0x01,
    SM_CAP_WRITABLE = 0x02,
};

// How often a subscriber wants pushed updates for a value.
enum SM_UpdateRate : uint8_t
{
    SM_RATE_ON_CHANGE = 0,
    SM_RATE_1HZ = 1,
    SM_RATE_5HZ = 2,
    SM_RATE_10HZ = 3,
    SM_RATE_FAST = 4, // "as fast as reasonable while the value is actively changing"
};

// Every packet transmitted over ESP-NOW starts with this, immediately
// followed by payloadLength bytes of one of the payload structs below
// (which one is determined by messageType).
#pragma pack(push, 1)
struct SM_Header
{
    uint8_t version;      // protocol version, currently 1
    uint8_t messageType;  // SM_MessageType

    uint32_t sourceID;      // sender's persistent device ID
    uint32_t destinationID; // recipient's device ID, or 0xFFFFFFFF for broadcast

    uint16_t sequence;      // sender-assigned, increments per message
    uint16_t payloadLength; // bytes of payload following this header
};
#pragma pack(pop)

// A device's identity - carried in SM_ANNOUNCE and embedded in
// SM_Heartbeat. deviceID is derived from the device's MAC address (see
// espnow_state.h's espnow_local_device_id()) so it's persistent without
// needing to be provisioned.
#pragma pack(push, 1)
struct SM_DeviceIdentity
{
    uint32_t deviceID;
    uint8_t deviceType;           // SM_DeviceType
    uint8_t protocolVersion;
    uint8_t firmwareVersionMajor;
    uint8_t firmwareVersionMinor;
    uint8_t firmwareVersionPatch;
    char friendlyName[24]; // e.g. "FN-POD-01" - null-terminated, null-padded
};
#pragma pack(pop)

// Payload for SM_PROVISION_REQUEST: identity plus a nonce that stays fixed
// for one continuous pairing attempt (regenerated only if the requester
// reboots or gives up and starts a fresh attempt). Every reply in the
// handshake below echoes it back, so the requester can tell a reply
// actually belongs to its current attempt rather than a stale one (e.g.
// from before it last rebooted, or another device's exchange it happened
// to overhear).
#pragma pack(push, 1)
struct SM_ProvisionRequest
{
    SM_DeviceIdentity identity;
    uint32_t nonce;
};
#pragma pack(pop)

// Payload for SM_PROVISION_HOLD, SM_PROVISION_REJECTED, and
// SM_PROVISION_ACK - just the echoed nonce from the SM_ProvisionRequest
// being responded to.
#pragma pack(push, 1)
struct SM_ProvisionNonce
{
    uint32_t nonce;
};
#pragma pack(pop)

// Sent by a provisioner (e.g. the CYD tester) to an unprovisioned device
// (e.g. the Atom S3 Lite FN pod) in reply to its SM_PROVISION_REQUEST.
// nonce must match that request's nonce - the requester ignores this
// message otherwise. No Wi-Fi credentials involved: ESP-NOW only needs
// both radios on the same channel, so that's the only thing handed over -
// the requester never associates with the AP or gets an IP address.
#pragma pack(push, 1)
struct SM_ChannelAssignment
{
    uint32_t nonce;
    uint8_t channel; // the provisioner's current Wi-Fi/ESP-NOW channel (1-14)
};
#pragma pack(pop)

#pragma pack(push, 1)
struct SM_Heartbeat
{
    SM_DeviceIdentity identity;
    uint32_t uptimeSeconds;
    uint8_t statusFlags; // device-specific (e.g. bit 0 = fault) - not yet defined
};
#pragma pack(pop)

// Describes one value or command a device exposes. minValue/maxValue only
// apply when valueType isn't SM_VAL_COMMAND; both 0 means "unbounded."
#pragma pack(push, 1)
struct SM_CapabilityDescriptor
{
    char name[16]; // e.g. "CurrentAZ", "SET_AZ" - null-terminated, null-padded
    uint8_t valueType; // SM_ValueType
    uint8_t unit;      // SM_Unit
    uint8_t flags;     // SM_CapabilityFlags bitmask
    float minValue;
    float maxValue;
};
#pragma pack(pop)

// A device's full capability list is announced as 1+ of these, all sharing
// the same deviceID, with fragmentIndex running 0..fragmentCount-1 - see
// the fragmentation note at the top of this file. Followed immediately by
// capabilityCount * SM_CapabilityDescriptor.
#pragma pack(push, 1)
struct SM_CapabilitiesPayload
{
    uint32_t deviceID;
    uint8_t fragmentIndex;
    uint8_t fragmentCount;
    uint8_t capabilityCount; // descriptors in *this* fragment
};
#pragma pack(pop)

#pragma pack(push, 1)
struct SM_SubscribeRequest
{
    uint32_t targetDeviceID;
    char capabilityName[16];
    uint8_t rate; // SM_UpdateRate - ignored for SM_UNSUBSCRIBE
};
#pragma pack(pop)

// SM_PING / SM_PONG payload - pingID is sender-chosen (e.g. millis()) and
// echoed back unchanged in the SM_PONG reply, so the sender can match a
// reply to the specific ping it sent and measure round-trip time.
#pragma pack(push, 1)
struct SM_PingPayload
{
    uint32_t pingID;
};
#pragma pack(pop)

// One named value's current (or newly-requested) reading - used for both
// SM_VALUE (device -> subscriber) and SM_SET_VALUE (subscriber -> device).
// Which union member is valid is determined by the capability's valueType.
#pragma pack(push, 1)
struct SM_ValuePayload
{
    char capabilityName[16];
    uint8_t valueType; // SM_ValueType
    union
    {
        bool boolValue;
        int32_t int32Value;
        uint32_t uint32Value;
        float floatValue;
    };
};
#pragma pack(pop)

#pragma pack(push, 1)
struct SM_CommandPayload
{
    char commandName[16];
    int32_t argument; // meaning is command-specific; 0 if unused
};
#pragma pack(pop)

// A persisted rename, not a one-shot display label - the receiver should
// save this (surviving reboot) and use it in every future
// SM_DeviceIdentity it sends (SM_ANNOUNCE/SM_HEARTBEAT/etc.), the same way
// this project's own devices persist their own friendly name locally
// (e.g. the CYD's g_config.espnow_friendly_name). Not folded into
// SM_CommandPayload since that struct's commandName[16] has no room for a
// full 24-char name alongside it.
#pragma pack(push, 1)
struct SM_RenamePayload
{
    char newName[24]; // matches SM_DeviceIdentity.friendlyName's size
};
#pragma pack(pop)

// FnMainProfileMatch, mirrored from the pod's fn_pcb085_profile.h - kept as
// a plain wire-format enum here (not #included) since the CYD has no
// reason to depend on that pod-side header. PCB-110 has zero confirmed
// address mappings anywhere in this project - kUnrecognized means "doesn't
// match PCB-085," never "confirmed PCB-110."
enum FnMainProfileMatch : uint8_t
{
    FN_MAIN_PROFILE_NONE = 0,
    FN_MAIN_PROFILE_PCB085 = 1,
    FN_MAIN_PROFILE_UNRECOGNIZED = 2,
};

// The FN pod's live/simulated FN-MAIN decode snapshot, broadcast whenever
// it changes (see main.cpp's fn_word_decoder.h-driven decode loop). There
// is no real protected FN-MAIN receive interface yet (see
// FN_OUTPUT_Tester_Handoff/docs/TESTER_ARCHITECTURE.md's "Interface
// Safety") - today this only ever reflects a simulated capture replay
// (simulating != false), never a live bus.
#pragma pack(push, 1)
struct SM_FnMainStatusPayload
{
    uint8_t simulating;      // bool - pod is currently replaying a simulated capture
    uint8_t profileMatch;    // FnMainProfileMatch
    uint8_t outputs[16];     // 0/1 per output - only indices with a confirmed address (currently 0-7) are ever set
    uint8_t analogCode;      // 0-15 - only meaningful once address 10110 has been seen this session
    uint8_t lastAddressBits; // most recently decoded 5-bit address, packed MSB-first (bit4=A1..bit0=A5) - for raw diagnostics, per this project's "never hide the raw address" rule
    char captureLabel[24];   // which embedded capture is currently playing (see the pod's fn_main_sim_captures.h) - button press cycles to the next one
};
#pragma pack(pop)

#pragma pack(push, 1)
struct SM_AckPayload
{
    uint16_t acknowledgedSequence;
};
#pragma pack(pop)

// What an FN 2-wire pod is actually putting on GPIO2 right now, broadcast
// (SM_STATUS) by the pod whenever it changes - see
// FN_OUTPUT_Tester_Handoff/CLAUDE.md's "Continuous State Requirement" and
// the pod's fn_pcb085_profile.h. Broadcast rather than unicast to the
// current CYD, matching the lesson from this project's ESP-NOW pairing
// bug: broadcast has proven reliable all session where synchronous
// unicast from certain call contexts has not (see
// PROJECT_MEMORY.md/ESPNOW_PROTOCOL.md's 2026-08-27 pairing entries).
enum FnTxMode : uint8_t
{
    FN_TX_MODE_OFF = 0,          // not transmitting on GPIO2 at all
    FN_TX_MODE_PLACEHOLDER = 1,  // looping the fixed captured idle frame - NOT a real per-model encoding (currently: PCB-110, which has no confirmed address map yet)
    FN_TX_MODE_REAL_ENCODED = 2, // continuously encoding tracked output/analog state into real FN address+data words (currently: PCB-085 only)
};

#pragma pack(push, 1)
struct SM_FnTxStatusPayload
{
    uint8_t txMode; // FnTxMode
};
#pragma pack(pop)

#pragma pack(push, 1)
struct SM_ErrorPayload
{
    uint16_t failedSequence;
    uint8_t errorCode; // not yet enumerated
    char message[32];  // null-terminated, null-padded, human-readable detail
};
#pragma pack(pop)
