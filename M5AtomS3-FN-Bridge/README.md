# M5Stack Atom S3 Lite - FN Two-Wire Pod

Starter firmware for using an M5Stack Atom S3 Lite as the physical
interface ("pod") to the UNA-DYN/FN two-wire bus, remote-controlled over
ESP-NOW by the [CYD-4.3-FN-Tester](../CYD-4.3-FN-Tester) touchscreen
tester.

## Scope of this starter

This firmware only establishes ShackMate ESP-NOW discovery and
communication with the CYD tester (and any other ShackMate device on the
same channel):

- Learns the mesh's ESP-NOW channel from the CYD automatically offering to
  pair (see "Channel provisioning" below) instead of a compile-time
  constant. **Never joins Wi-Fi** - no SSID/password anywhere, no IP
  address, ever. ESP-NOW only needs a matching radio channel, so that's
  the only thing this hands over.
- Broadcasts `SM_DISCOVER` at boot (once provisioned).
- Responds to others' `SM_DISCOVER` with `SM_ANNOUNCE`.
- Sends `SM_HEARTBEAT` every 10s.
- Tracks other ShackMate devices it hears from, logged to Serial.
- Onboard RGB LED shows link status (see below) since the Lite has no
  display.
- Front button: hold 5s any time to factory-reset (forget the channel);
  short press sends `SM_PING` as a manual connectivity test once
  provisioned (see "Button" below).
- Once paired, the CYD's Diagnostics page can send it `SM_COMMAND`
  `"REBOOT"` or `"FACTORY_RESET"` remotely - the pod ACKs before acting
  (see "Remote commands" below).
- Once provisioned, enables Wi-Fi modem-sleep (`WIFI_PS_MIN_MODEM`) for a
  modest power save - see the caveat under "Channel provisioning" below.

It does not yet talk to the *real* FN two-wire bus - no isolation/
protection circuitry exists yet, so nothing here is connected to actual
FN hardware (see "Interface Safety" in
`FN_OUTPUT_Tester_Handoff/docs/TESTER_ARCHITECTURE.md`). It can, however,
replay a captured FN waveform on bare GPIO for bench verification with a
scope/logic analyzer - see "FN bus waveform replay" below.

**No network OTA.** Since this pod never joins Wi-Fi or gets an IP
address, `ArduinoOTA` (which needs mDNS/UDP over IP) can't work here.
Reflash over USB.

## Hardware

M5Stack **Atom S3 Lite** (ESP32-S3, no LCD/IMU unlike the full AtomS3).

| Function | Pin |
|---|---|
| Onboard RGB LED (4x WS2812, driven identically) | GPIO35 |
| Front button | GPIO41 (active LOW) |
| Grove port | GPIO1 (G2) - not used yet; GPIO2 (G1) - FN bus bench-test replay TX, see below |
| USB-C | native USB CDC (flashing + `Serial` - requires `ARDUINO_USB_CDC_ON_BOOT=1`, set in `platformio.ini`) |

LED colors:
- **Green (blinking)** - unprovisioned, sweeping channels and
  broadcasting `SM_PROVISION_REQUEST`, waiting to be found.
- **Cyan** - a provisioner replied `SM_PROVISION_HOLD`; parked on that
  channel awaiting its decision.
- **Blue (solid)** - provisioned and on the ESP-NOW mesh. This is the only
  provisioned-state color now - it no longer distinguishes "peer seen
  recently" from "idle," since the pod is expected to be reachable for
  remote commands at any time regardless of recent traffic.
- **White flash** - a button-press `SM_PING` got its `SM_PONG` back (see
  "Button" below).
- **Blinking red** - `esp_now_init()` failed (halts), or a few flashes to
  confirm a factory-reset request before restarting.
- A bright white flash at boot is a self-test independent of all of the
  above - if you never see that, it's an LED/wiring/library problem, not
  a pairing one.

## Channel provisioning

No channel is compiled into this firmware. On first boot (no saved
channel in NVS), it sweeps `SM_PROVISIONING_CHANNEL_MIN..MAX` (2.4GHz
channels 1-11), dwelling ~400ms per channel and broadcasting
`SM_PROVISION_REQUEST` - the CYD never has to leave its own network to
find it.

To pair it:
1. Flash and power on the pod - its LED should be orange.
2. On the CYD tester, just wait - it listens passively at all times. Once
   the pod's sweep reaches the CYD's channel, a "New FN 2-Wire Pod
   detected - Pair?" dialog pops up on the CYD automatically (this can
   take a few seconds, depending on where in the 1-11 sweep the CYD's own
   channel falls).
3. The pod's LED goes cyan (holding, waiting on the CYD's decision) as
   soon as that dialog appears.
4. Tap **Pair** on the CYD. The pod saves the channel number, ACKs, and
   restarts, rejoining the ShackMate mesh on that channel (LED goes blue,
   then green once it hears the CYD). The CYD shows its own follow-up
   dialog confirming the ACK arrived (or a timeout if it didn't).
   Tapping **Ignore** instead sends the pod back to sweeping immediately.

See `../CYD-4.3-FN-Tester/ESPNOW_PROTOCOL.md`'s "Channel provisioning"
section for the full wire-level handshake, including why there's a nonce.

## FN bus waveform replay (bench test only)

`src/fn_bus_tx.cpp`/`.h` can loop a captured FN two-wire waveform on GPIO2
(Grove G1), driven by the ESP32-S3's RMT peripheral for precise hardware
timing. **This is bare, unprotected GPIO - do not connect it to the real
FN two-wire bus.** No isolation, level shifting, or protection circuitry
exists yet for this pod (see the CYD project's
`FN_OUTPUT_Tester_Handoff/docs/TESTER_ARCHITECTURE.md`'s "Interface
Safety" section). This exists purely so the generated waveform can be
bench-verified with a scope/logic analyzer against the original capture
before any real hardware is ever touched.

`src/fn_reference_frame.h` is a pre-generated data table (regenerate via
`../CYD-4.3-FN-Tester/FN_OUTPUT_Tester_Handoff/saleae/analysis/gen_rmt_table.py`
if the source frame changes) holding one clean 50.0ms period extracted
from `captures/digital.csv`, noise-filtered down to fit the ESP32-S3's
hard RMT hardware-loop limit (256 words - only 4 TX RMT channels x 64
words each on this chip). See that project's `docs/EXPERIMENT_LOG.md`
(Experiment 002) and `docs/FN_PROTOCOL_FINDINGS.md` for how/why this
specific frame was chosen and what the noise-filtering step trades away -
this is a faithful-but-filtered replay of one known-good idle/keepalive
capture, not a decoded, semantically-understood protocol implementation.

Controlled over Serial (no CYD UI or ESP-NOW command wiring yet - this is
a firmware-level bench-test hook, not an end-user feature):

```text
tx      start looping the reference frame on GPIO2
stop    stop transmission
```

Connect over `pio device monitor` (or any serial terminal) and type either
command, followed by Enter.

**Restoring to defaults:** hold the front button for 5s *any time* (not
just at boot) to forget the saved channel and restart into pairing mode -
no reflashing or power-cycling needed. The LED flashes red a few times to
confirm before it restarts.

## Button

- **Hold 5s** (any time): factory reset - forgets the saved channel,
  restarts into pairing mode.
- **Short press** (while provisioned): sends `SM_PING` to test
  connectivity with whatever ShackMate device replies (in practice, the
  CYD tester) - the LED flashes white on a matching `SM_PONG`, or Serial
  logs a timeout after 3s with none.

## Remote commands

Once paired, the CYD's ESP NOW page -> Diagnostics -> a paired pod's
**Reboot** / **Restore** buttons send `SM_COMMAND` unicasts:

| `commandName`     | Pod behavior |
|--------------------|--------------|
| `"REBOOT"`          | `ESP.restart()` after a short delay. |
| `"FACTORY_RESET"`   | Same as the 5s button-hold gesture: forgets the saved channel and restarts into sweeping/pairing mode. |

The pod always replies `SM_ACK` first (visible in the CYD's Monitor page
traffic log) before acting on a recognized command name; unrecognized
names are ACKed but otherwise ignored. Fire-and-forget from the CYD side -
no retry if the ACK never arrives.

## Setup

```sh
pio run -e m5stack-atoms3 -t upload --upload-port /dev/cu.usbmodemXXXX
pio device monitor
```

Check `ls /dev/cu.*` for the actual port. After first flashing, pair it
per "Channel provisioning" above.

## Protocol

Shares the ShackMate ESP-NOW protocol with the CYD tester -
`src/espnow_protocol.h` here is a manually-synced copy of
`../CYD-4.3-FN-Tester/src/espnow_protocol.h`. This device identifies as
`SM_DEVICE_FN_2WIRE_POD` (value 9), default friendly name `"FN-POD-01"`.
See `../CYD-4.3-FN-Tester/ESPNOW_PROTOCOL.md` for the full protocol
writeup.

**If you change `espnow_protocol.h` here, mirror the change in the CYD
project's copy too** (and vice versa) - there's no shared library between
the two PlatformIO projects yet.

## Not yet designed

- The actual FN two-wire physical interface (signal levels, isolation,
  level shifting/protection) - see the CYD project's
  `FN_OUTPUT_Tester_Handoff/docs/TESTER_ARCHITECTURE.md`. A candidate
  transmit waveform exists now (`fn_bus_tx.cpp`), but it's bare GPIO with
  nothing to safely connect it to real hardware yet.
- Real FN *receive* capability - only transmit/replay exists so far.
- A configurable friendly name (currently the constant `"FN-POD-01"` in
  `main.cpp`) if more than one pod is ever deployed - the CYD can already
  remember up to 10 paired devices, but they'd all currently announce the
  same name unless this firmware constant is changed per unit.
- Any real network/IP connectivity for this pod (deliberately out of
  scope - see "No network OTA" above). If a future feature needs that,
  it'll need its own opt-in Wi-Fi join, separate from ESP-NOW channel
  provisioning.
- The provisioning handshake is unauthenticated/unencrypted at the
  ESP-NOW layer (see ESPNOW_PROTOCOL.md's note on why encryption was
  tried and reverted) - low-stakes now that it only carries a channel
  number, not credentials.
- Capabilities/values (`SM_CAPABILITIES`, `SM_VALUE`,
  `SM_SUBSCRIBE`/`SM_UNSUBSCRIBE`, `SM_SET_VALUE`) - not implemented.
  `SM_COMMAND` itself is implemented, but only for the two pod-lifecycle
  commands above ("Remote commands"), not a general capability-command
  model yet.
- Firmware update over ESP-NOW - explicitly a **future** option (the
  CYD's Diagnostics page has a disabled "Update" placeholder for it), not
  designed yet. Would need a network path this pod doesn't have today (see
  "No network OTA" above) or a chunked-transfer scheme over ESP-NOW itself.
