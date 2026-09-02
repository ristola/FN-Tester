# ShackMate ESP-NOW Device Discovery and Control Protocol

Wire protocol for ESP-NOW communication between ShackMate ESP32 devices -
this CYD panel, antenna rotors, power controllers, SWR meters, weather
stations, Meshtastic interfaces, status displays, button panels, and future
hardware. Goal: any new ShackMate device can announce "here I am, this is
what I am, these are the values I provide, and these are the commands you
can send me," with no central server, MQTT broker, or Internet connection
required.

Definitions live in [`src/espnow_protocol.h`](src/espnow_protocol.h) - this
document explains what they mean.

**Status:** discovery/presence is implemented (see
[`src/espnow_state.h`](src/espnow_state.h)) - on boot the CYD broadcasts
`SM_DISCOVER`, responds to others' `SM_DISCOVER` with `SM_ANNOUNCE`, sends
`SM_HEARTBEAT` every 10s, and tracks every device it hears from (by
`deviceID`, with last-seen time) in a local table shown on the ESP NOW page
in the hamburger drawer. Channel provisioning (see below) is also
implemented: the CYD listens passively on its own network at all times and
pops up a "Pair?" confirmation whenever an unprovisioned pod's sweep
happens to reach its channel - it never has to leave its own network to
find one. `SM_PING`/`SM_PONG` (a paired pod's button press) and a small set
of remote `SM_COMMAND`s (`REBOOT`, `FACTORY_RESET`) are also implemented -
see the ESP NOW page's Monitor and Diagnostics sub-pages below.
Capabilities, subscriptions, `SM_SET_VALUE`, and general (non-pod) commands
are still data-model only - not sent, received, or acted on yet.

## ESP NOW page (CYD UI)

The ESP NOW page in the hamburger drawer has three sub-pages
(`src/ui_espnow.cpp`):

- **Setup** - this device's friendly name (`SM_DeviceIdentity.friendlyName`,
  persisted to NVS).
- **Monitor** - a live, scrolling log of every ESP-NOW message sent or
  received (direction, message type, peer, age), backed by a fixed-size
  ring buffer (`kMaxTrafficLogEntries` = 40 in `espnow_state.h`) so it
  reflects everything on the wire, not just message types the CYD acts on;
  plus a "Send Test Ping" button that broadcasts a one-off `SM_PING`.
- **Diagnostics** - the persisted paired-bridge list, each with **Reboot**
  (`SM_COMMAND "REBOOT"`), **Restore** (`SM_COMMAND "FACTORY_RESET"` - the
  pod forgets its saved channel and restarts into sweeping/pairing mode,
  equivalent to its own 5s button-hold gesture), **Forget** (removes the
  pod from the CYD's own list only - doesn't touch the pod itself), and a
  disabled **Update (soon)** placeholder for a future firmware-over-ESP-NOW
  path (not implemented - see "Not yet designed" below).

## Transport

ESP-NOW is Espressif's connectionless, point-to-multipoint radio protocol.
It rides on the same 2.4GHz radio as Wi-Fi station mode and requires all
peers to be on the **same channel** - but *only* the channel: ESP-NOW
itself needs no AP association, no IP address, no DHCP. A device that's
also joined to a Wi-Fi network (like this CYD) is on whatever channel its
access point uses (`WiFi.channel()`, shown on the ESP NOW page); a device
that only needs the ESP-NOW mesh (like the FN pod) never has to join
Wi-Fi at all - see "Channel provisioning" below for how it still ends up
on the right channel without a laptop or compile-time constant.

Every packet is an `SM_Header` immediately followed by `payloadLength`
bytes of a message-specific payload.

**250-byte payload limit.** ESP-NOW caps each packet at 250 bytes total.
A device with many capabilities (a rotor might advertise 10 values + 6
commands) won't fit its whole capability list in one `SM_CAPABILITIES`
packet - see "Capability announcement" below for how that's split.

## Device identity

```c
struct SM_DeviceIdentity {
    uint32_t deviceID;              // persistent, derived from MAC (see below)
    uint8_t  deviceType;            // SM_DeviceType
    uint8_t  protocolVersion;
    uint8_t  firmwareVersionMajor;
    uint8_t  firmwareVersionMinor;
    uint8_t  firmwareVersionPatch;
    char     friendlyName[24];      // e.g. "Ultimate - 01", "FN-POD-01"
};
```

`deviceID` is derived from the last 4 bytes of the device's Wi-Fi station
MAC address (`espnow_local_device_id()`) - persistent across reboots with
nothing to provision or store. `friendlyName` is currently hardcoded per
device (`"Ultimate - 01"` for this CYD); promoting it to a configurable,
persisted field is future work if multiple devices of the same type need
distinct names.

### Device types (`SM_DeviceType`)

Append-only - a value's meaning must never change once devices using it
exist in the field.

| Value | Name                          |
|-------|-------------------------------|
| 0     | `SM_DEVICE_GENERIC`           |
| 1     | `SM_DEVICE_C64_ULTIMATE_GATEWAY` (this CYD) |
| 2     | `SM_DEVICE_ANTENNA_ROTOR`     |
| 3     | `SM_DEVICE_AUX_POWER_CONTROLLER` |
| 4     | `SM_DEVICE_SWR_METER`         |
| 5     | `SM_DEVICE_WEATHER_STATION`   |
| 6     | `SM_DEVICE_MESHTASTIC_INTERFACE` |
| 7     | `SM_DEVICE_STATUS_DISPLAY`    |
| 8     | `SM_DEVICE_BUTTON_PANEL`      |
| 9     | `SM_DEVICE_FN_2WIRE_POD` (M5 Atom S3 Lite - physical interface "pod" to the FN two-wire bus, see `../M5AtomS3-FN-Bridge`) |

## Message types

### Discovery (0x01 - 0x0F)

| Value | Name                      | Payload                  | Purpose |
|-------|---------------------------|---------------------------|---------|
| 0x01  | `SM_DISCOVER`             | (none)                    | Broadcast: "who's out there?" - a freshly-powered device sends this so existing devices immediately re-announce, rather than waiting for their next heartbeat. |
| 0x02  | `SM_ANNOUNCE`              | `SM_DeviceIdentity`       | Broadcast, or unicast reply to `SM_DISCOVER`: "here I am." |
| 0x03  | `SM_HEARTBEAT`              | `SM_Heartbeat`            | Periodic broadcast: identity + uptime + status flags, so peers can track last-seen without a full re-announce. |
| 0x04  | `SM_CAPABILITIES`            | `SM_CapabilitiesPayload` + descriptors | Reply: this device's values/commands (see below). |
| 0x05  | `SM_CAPABILITIES_REQUEST`     | (none, or targeted via header's destinationID) | Unicast: "send me your capabilities." |
| 0x06  | `SM_PROVISION_REQUEST`        | `SM_ProvisionRequest` | Broadcast, while channel-sweeping: "I don't know the mesh channel yet, pair me." See "Channel provisioning" below. |
| 0x07  | `SM_PROVISION_HOLD`           | `SM_ProvisionNonce` | Unicast, provisioner -> requester: "I see you - stop sweeping and wait here." |
| 0x08  | `SM_PROVISION_CHANNEL`        | `SM_ChannelAssignment` | Unicast reply: "here's the channel to use." |
| 0x09  | `SM_PROVISION_REJECTED`       | `SM_ProvisionNonce` | Unicast, provisioner -> requester: "declined - resume sweeping." |
| 0x0A  | `SM_PROVISION_ACK`            | `SM_ProvisionNonce` | Unicast, requester -> provisioner: "channel saved, rejoining." |

```c
struct SM_Heartbeat {
    SM_DeviceIdentity identity;
    uint32_t uptimeSeconds;
    uint8_t  statusFlags;   // device-specific, not yet defined
};
```

### Data (0x10 - 0x1F)

| Value | Name             | Payload               | Purpose |
|-------|------------------|-------------------------|---------|
| 0x10  | `SM_STATUS`       | device-specific         | General status, not yet formalized. |
| 0x11  | `SM_VALUE`         | `SM_ValuePayload`       | Push a single named value's current reading (e.g. in response to a subscription). |
| 0x12  | `SM_SUBSCRIBE`      | `SM_SubscribeRequest`   | "Push me updates for this capability at this rate" instead of the subscriber having to poll or the device having to broadcast everything constantly. |
| 0x13  | `SM_UNSUBSCRIBE`     | `SM_SubscribeRequest` (rate ignored) | Cancels a subscription. |
| 0x14  | `SM_PING`            | `SM_PingPayload`        | **Implemented.** Broadcast: manual connectivity test - a paired FN pod sends this on a short press of its physical button; the CYD's "Send Test Ping" button (Monitor page) sends one too. |
| 0x15  | `SM_PONG`            | `SM_PingPayload` (echoed) | **Implemented.** Reply to `SM_PING`, same `pingID`. The pod flashes its LED white on receipt; the CYD records it as the "last ping" shown on the ESP NOW page. |

```c
struct SM_PingPayload { uint32_t pingID; };
```

```c
enum SM_UpdateRate {
    SM_RATE_ON_CHANGE = 0,
    SM_RATE_1HZ, SM_RATE_5HZ, SM_RATE_10HZ,
    SM_RATE_FAST,   // as fast as reasonable while the value is actively changing
};

struct SM_SubscribeRequest {
    uint32_t targetDeviceID;
    char     capabilityName[16];
    uint8_t  rate;   // SM_UpdateRate
};
```

### Commands (0x20 - 0x2F)

| Value | Name          | Payload             | Purpose |
|-------|---------------|-----------------------|---------|
| 0x20  | `SM_SET_VALUE` | `SM_ValuePayload`     | Set a writable value. |
| 0x21  | `SM_COMMAND`    | `SM_CommandPayload`   | Trigger a named action (e.g. a rotor's `GO`, `STOP`, `PARK`). |

```c
struct SM_CommandPayload {
    char    commandName[16];
    int32_t argument;   // meaning is command-specific; 0 if unused
};
```

**`SM_COMMAND` is partially implemented**, for the CYD's Diagnostics page
controlling a paired FN pod only - not the general "any capability's
commands" case. The pod ACKs (`SM_ACK`) every recognized `SM_COMMAND`
before acting on it (visible in the Monitor page's traffic log), then:

| `commandName`     | Pod behavior |
|--------------------|--------------|
| `"REBOOT"`          | `ESP.restart()` after a short delay. |
| `"FACTORY_RESET"`   | Forgets its saved channel (NVS), then restarts into channel-sweeping/pairing mode - equivalent to its own 5s button-hold gesture. |

Any other `commandName` is ACKed but otherwise ignored (`argument` is
unused by both of the above). Per the original design goal, general
capability commands and configuration changes should eventually use
acknowledged, preferably encrypted unicast (ESP-NOW supports per-peer
encryption, at the cost of registering each peer) - beyond the two pod
commands above, this is not yet implemented; those message types still
exist only as definitions.

### Responses (0x30 - 0x3F)

| Value | Name       | Payload          | Purpose |
|-------|------------|--------------------|---------|
| 0x30  | `SM_ACK`    | `SM_AckPayload`    | Acknowledges a message by sequence number. |
| 0x31  | `SM_ERROR`   | `SM_ErrorPayload`  | Reports a failure processing a received message. |

```c
struct SM_AckPayload { uint16_t acknowledgedSequence; };

struct SM_ErrorPayload {
    uint16_t failedSequence;
    uint8_t  errorCode;   // not yet enumerated
    char     message[32];
};
```

## Channel provisioning

A device that doesn't yet know the mesh's ESP-NOW channel (e.g. a
factory-fresh M5 Atom S3 Lite FN pod) can't reach anyone else on it. Since
ESP-NOW itself needs nothing but a matching channel - no AP association, no
IP address - this handshake hands over exactly that and nothing else: no
Wi-Fi credentials are ever exchanged, and the requester (the pod) never
joins Wi-Fi at all, at any point. Rather than making the provisioner (the
CYD tester) leave *its* network to go find one - which would drop it off
the air for the duration - the unprovisioned device does the searching: it
sweeps `SM_PROVISIONING_CHANNEL_MIN..MAX` (2.4GHz channels 1-11), dwelling
briefly on each one and broadcasting `SM_PROVISION_REQUEST`, until the
sweep happens to land on whatever channel a provisioner is already sitting
on.

```c
struct SM_ProvisionRequest {
    SM_DeviceIdentity identity;
    uint32_t           nonce;   // fixed for this attempt - see below
};
```

The **nonce** identifies one continuous pairing attempt (regenerated only
if the requester reboots or gives up and starts sweeping again from
scratch). Every reply below echoes it back, and the requester ignores any
reply whose nonce doesn't match its current attempt - protection against
acting on a stale reply left over from a previous attempt, or a reply
aimed at some other device's overlapping exchange.

The handshake, once a provisioner hears a request:

1. **Provisioner -> requester: `SM_PROVISION_HOLD`** (`SM_ProvisionNonce`,
   echoing the request's nonce). Sent automatically, no human involved yet
   - its only job is to tell the requester "I see you, stop sweeping and
   wait right here," because the next step needs a human to look at a
   confirmation dialog and decide, which takes far longer than one
   channel dwell (~400ms on the FN pod).
2. The provisioner's UI asks its user to confirm (e.g. the CYD's "New FN
   2-Wire Pod detected - Pair?" popup).
3. **On accept - provisioner -> requester: `SM_PROVISION_CHANNEL`**
   (`SM_ChannelAssignment`, the provisioner's own current
   `WiFi.channel()`), and the provisioner records the requester's
   MAC/identity in its own persisted paired-device list (the CYD keeps up
   to `kMaxPairedBridges` = 10, so more than one pod - or pod type - can
   be remembered at once).
   **On decline - provisioner -> requester: `SM_PROVISION_REJECTED`**
   (`SM_ProvisionNonce`), so the requester resumes sweeping immediately
   instead of waiting out its own hold timeout.
4. The requester saves the channel number to NVS, replies
   **`SM_PROVISION_ACK`** (`SM_ProvisionNonce`), and restarts to rejoin
   the mesh on that channel - still without ever touching Wi-Fi.

```c
struct SM_ProvisionNonce { uint32_t nonce; };

struct SM_ChannelAssignment {
    uint32_t nonce;   // must match the requester's current attempt
    uint8_t  channel; // the provisioner's current Wi-Fi/ESP-NOW channel (1-14)
};
```

If nothing happens within the requester's hold timeout (30s in the FN
pod), or the provisioner's own pending-request timeout (35s in the CYD -
deliberately a little longer, so it doesn't send a pointless
`SM_PROVISION_REJECTED` to a requester that's already given up and moved
on), each side quietly resumes on its own.

**LED (pod only - the CYD has no addressable LED):** solid/blinking green
while unprovisioned and sweeping, solid blue once provisioned and joined to
the mesh, cyan while holding for a pairing decision, a brief white flash on
a successful `SM_PONG`, blinking red on error.

**Power (pod only):** once provisioned, the pod calls
`esp_wifi_set_ps(WIFI_PS_MIN_MODEM)`. This is Wi-Fi modem-sleep power
saving, *not* turning the radio off - ESP-NOW still needs it active to
receive - and since the pod never associates to an AP, there's no DTIM
interval to actually sleep against, so its real-world benefit on battery is
unconfirmed. Deeper duty-cycled sleep was deliberately not pursued: it
would conflict with wanting the pod reachable for remote `SM_COMMAND`s "any
time," not just on a schedule.

**Verification.** Sending `SM_PROVISION_CHANNEL` isn't proof the pod
received it, so the CYD tracks the outcome (`espnow_pair_outcome()` in
`espnow_state.h`) and shows a follow-up dialog once confirmed (or the wait
times out after 5s) - see `ui_shell.cpp`'s `poll_ack_dialog`. The pod does
send `SM_PROVISION_ACK` (unicast, 4x with a short delay between, since a
lone fire-and-forget packet is exactly the kind most likely to get lost),
but real hardware testing found this specific unicast send unreliable in
practice - it would consistently never arrive when sent from within the
pod's `on_provision_channel()` handler, regardless of several unrelated
bugs fixed along the way (a hold-timeout race, a stale MAC pointer,
concurrent heartbeat traffic, flash-write timing), while every broadcast
this pod sends has been reliable. So the CYD's *primary* confirmation
signal is actually the pod's `SM_ANNOUNCE` broadcast, sent immediately
after rejoining on the new channel in `enter_joined_mode()` - matched
against the expected `deviceID` in `on_espnow_recv()`'s `SM_ANNOUNCE`
case. `SM_PROVISION_ACK` is kept as a secondary, best-effort signal in
case it does get through, but isn't depended on.

**Encryption - reverted.** This handshake originally encrypted
`SM_PROVISION_CHANNEL`/`SM_PROVISION_ACK` using ESP-NOW's built-in
per-peer AES support (a shared compile-time PMK/LMK). In practice this
didn't work: encryption requires *both* sides to have already registered
each other as an encrypted peer before the first encrypted packet
arrives, but the requester has no way to do that in advance for a
provisioner it's never talked to before - real hardware testing confirmed
messages were silently undecryptable. Reverted to plain messages for
reliability. This also matters less now than it did when this carried
real Wi-Fi credentials: a channel number isn't much of a secret. Revisit
with a real handshake if that ever changes.

## Capabilities

A capability is one named value or command a device exposes:

```c
struct SM_CapabilityDescriptor {
    char    name[16];      // e.g. "CurrentAZ", "SET_AZ"
    uint8_t valueType;     // SM_ValueType
    uint8_t unit;          // SM_Unit
    uint8_t flags;         // SM_CapabilityFlags bitmask
    float   minValue;      // range, if applicable; 0/0 = unbounded
    float   maxValue;
};
```

`SM_ValueType`: `SM_VAL_BOOL`, `SM_VAL_INT32`, `SM_VAL_UINT32`,
`SM_VAL_FLOAT32`, or `SM_VAL_COMMAND` (no value - this entry is an action,
not a readable/writable value).

`SM_Unit`: `SM_UNIT_NONE`, `_DEGREES`, `_METERS`, `_METERS_PER_SEC`,
`_VOLTS`, `_AMPS`, `_WATTS`, `_CELSIUS`, `_PERCENT`, `_HZ`, `_DBM`,
`_ENUM` (a small fixed set of integers specific to that capability, e.g.
Direction: 0=CW, 1=CCW - documented per-device, not by this protocol), or
`_BOOLEAN`.

`SM_CapabilityFlags`: `SM_CAP_READABLE` and/or `SM_CAP_WRITABLE`
(bitmask - a command-only entry would have neither set, or the protocol
could treat `SM_VAL_COMMAND` as implicitly neither; not yet decided).

### Capability announcement (fragmentation)

A device's full capability list may not fit in one 250-byte packet, so it's
sent as 1 or more `SM_CAPABILITIES` packets, all sharing the same
`deviceID`, `fragmentIndex` running `0 .. fragmentCount-1`:

```c
struct SM_CapabilitiesPayload {
    uint32_t deviceID;
    uint8_t  fragmentIndex;
    uint8_t  fragmentCount;
    uint8_t  capabilityCount;   // descriptors in *this* fragment
    // followed by capabilityCount * SM_CapabilityDescriptor
};
```

A receiver reassembles the full list by collecting all fragments for a
given `deviceID` before treating the capability set as complete.

### Worked example: antenna rotor

A `SM_DEVICE_ANTENNA_ROTOR` might advertise:

- Values: `CurrentAZ`, `CurrentEL` (degrees, readable), `TargetAZ`,
  `TargetEL` (degrees, readable+writable), `Moving`, `Fault`, `Parked`
  (bool, readable), `Direction`, `Speed` (enum/percent, readable).
- Commands: `SET_AZ`, `SET_EL`, `GO`, `STOP`, `PARK`, `JOG`.

A generic ShackMate display (this CYD, an M5 Dial, etc.) could discover the
rotor, read its capability list, and build a basic monitoring/control
screen automatically - without any rotor-specific code - by matching on
capability names, types, and units rather than hardcoding what a "rotor"
is.

## Local device table

Each device maintains a table of other devices it's seen (`SM_KnownDevice`
in `espnow_state.h`), keyed by `deviceID` with a last-seen timestamp,
refreshed by incoming `SM_ANNOUNCE`/`SM_HEARTBEAT` (once receive is wired
up). This is what lets an interface like the ESP NOW page show which
ShackMate devices are currently online.

## Not yet designed / open questions

- Sending/receiving `SM_CAPABILITIES` (and its fragmentation), `SM_VALUE`,
  `SM_SUBSCRIBE`/`SM_UNSUBSCRIBE`, `SM_SET_VALUE`, and general
  (non-pod-specific) `SM_COMMAND`s - see "Commands" above for the two pod
  commands that *are* implemented.
- **Firmware update over ESP-NOW** - the Diagnostics page's "Update"
  button is a disabled placeholder for this; explicitly a future option,
  not designed yet. The pod currently has no network path for new firmware
  at all (see its README) - reflash over USB only.
- Encryption for unicast commands, including the provisioning handshake
  itself (tried ESP-NOW's per-peer AES encryption, reverted - see
  "Channel provisioning" above).
- A device that also needs real Wi-Fi/IP connectivity (not just ESP-NOW)
  still has no provisioning path here - channel provisioning above
  deliberately never joins Wi-Fi at all, which is why the FN pod has no
  network OTA (see its README).
- Optional repeater/routing layer for coverage beyond direct radio range.
- `SM_STATUS` payload shape, `statusFlags` bit meanings, `errorCode`
  values.
- Bridging behavior for devices (like this CYD) that are simultaneously on
  Wi-Fi and ESP-NOW - e.g. publishing selected C64 Ultimate REST API data
  over ESP-NOW.
