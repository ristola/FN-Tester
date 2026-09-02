# Project Memory — CYD 4.3" FN Tester

Working notes for picking this project back up across sessions. Not
end-user documentation (see [README.md](README.md) for that) — this is
scratch/context for whoever (human or AI) resumes work here.

## Stack recap

- Sunton **ESP32-8048S043C** ("CYD"), ESP32-S3, 800x480 RGB parallel IPS +
  GT911 capacitive touch, 16MB flash / 8MB PSRAM, microSD, USB-C.
- PlatformIO, `framework = arduino`, custom board JSON in `boards/`.
- LovyanGFX for display/touch (`include/lgfx_config.h`).
- LVGL v9.5, config pinned in `include/lv_conf.h`.
- Build: `pio run` / `pio run -t upload` / `pio device monitor`. OTA env:
  `pio run -e esp32-8048S043C-ota -t upload`.

## UI file map

- `src/ui_shell.cpp` — top bar (title, hamburger `menu_btn` ->
  `LV_SYMBOL_LIST`, live Wi-Fi signal-bar icon) + slide-in drawer
  (`ui_shell_build_pool`) with `make_drawer_btn(parent, icon, text, cb)`
  rows: Wi-Fi (`LV_SYMBOL_WIFI`) / Esp Now (`LV_SYMBOL_BLUETOOTH`) / Setup
  (`LV_SYMBOL_SETTINGS`).
- `src/ui_home.cpp` — Home tab: Wi-Fi status label + three mode tiles
  (`make_mode_tile`) opening `ui_fn_main`/`ui_fn_output`/`ui_capture_learn`
  placeholder screens (all "not implemented yet" stubs for now).
- `src/ui_boot_menu.cpp` — hold-on-boot recovery overlay (Restore Defaults
  / Erase SD Card / Exit), `make_menu_btn` helper (plain text, no icons).
- `src/ui_wifi_setup.cpp`, `src/ui_setup.cpp`, `src/ui_espnow.cpp` — other
  full-screen overlays opened from the drawer.
- `src/ui_boot_splash.cpp` — full-screen boot logo shown while Wi-Fi comes
  up (`src/generated/boot_logo.*`).

## LVGL notes for this project

- Built-in symbol font glyphs (no image assets needed for simple menu
  icons) — see
  `.pio/libdeps/esp32-8048S043C/lvgl/src/font/lv_symbol_def.h` for the
  full list. Ones likely useful here: `LV_SYMBOL_WIFI`,
  `LV_SYMBOL_BLUETOOTH`, `LV_SYMBOL_SETTINGS`, `LV_SYMBOL_GPS`,
  `LV_SYMBOL_HOME`, `LV_SYMBOL_LIST`, `LV_SYMBOL_REFRESH`,
  `LV_SYMBOL_SD_CARD`, `LV_SYMBOL_TRASH`, `LV_SYMBOL_POWER`,
  `LV_SYMBOL_WARNING`.
- Enabled font sizes in `lv_conf.h`: Montserrat 24 / 28 / 34 only (20, 22
  disabled) — stick to those unless you enable more.
- Icon + label pairing pattern used in drawer buttons: a borderless
  `lv_obj` flex row (`LV_FLEX_FLOW_ROW`, center/center align, column pad)
  centered on the button, containing an icon label then a text label —
  keeps the pair centered as a unit regardless of glyph width.
- First-render fragility: this board's LVGL pipeline has been flaky on
  the very first render after boot. Existing code works around it by
  deferring things like status polling via `lv_timer_create` instead of
  calling synchronously at create time (see `ui_home_start()` comment,
  and `ui_wifi_setup.cpp`'s `kMaxScanResults` pre-allocated pool note).

## Placeholders still to revisit (carried over from README)

- `espnow_state.cpp`'s `fill_local_identity()` — `deviceType` is
  `SM_DEVICE_GENERIC`; pick/add a real `SM_DeviceType` in
  `espnow_protocol.h`.
- `config.h`'s default `espnow_friendly_name` ("FN Tester - 01") and
  `ui_shell.cpp`'s `kAppTitle` ("FN Tester") are placeholder names.
- `main.cpp`'s `ArduinoOTA.setHostname("cydfn")` / OTA env's
  `upload_port` in `platformio.ini` need a project-specific hostname.
- NVS namespace in `config.cpp` is `"fntester"`.
- `ui_fn_main.cpp`/`ui_fn_output.cpp`/`ui_capture_learn.cpp` are stub
  screens ("not implemented yet") — real content depends on the FN
  protocol reverse-engineering work in `FN_OUTPUT_Tester_Handoff/`.

## Companion project

- **`../M5AtomS3-FN-Bridge`** — separate PlatformIO project: M5 Atom S3
  Lite firmware ("pod") that will eventually be the physical interface to
  the FN two-wire bus, talking to this CYD over ESP-NOW (ShackMate
  protocol, `SM_DEVICE_FN_2WIRE_POD`). Currently
  discovery/heartbeat/Wi-Fi-provisioning only — no FN bus logic yet.
  `src/espnow_protocol.h` is duplicated between the two projects (no
  shared library) - **mirror any protocol header change in both places.**
- Pairing model (as of 2026-08-27): the pod sweeps ESP-NOW channels
  1-11 broadcasting `SM_PROVISION_REQUEST`; the CYD stays on its own
  network the whole time and just listens, popping up a "Pair?" dialog
  (`ui_shell.cpp`'s `poll_pair_request`) whenever it hears one. The CYD
  never leaves its network to provision a pod — the earlier
  "Scan for Bridges" design (CYD hops to a fixed channel) was replaced
  because it briefly dropped the CYD off its own network. Paired pods
  persist in NVS (`espnow_state.cpp`'s `s_paired_bridges`, up to
  `kMaxPairedBridges` = 10), shown/managed on the ESP NOW page.
- **The pod never joins Wi-Fi at all** (as of 2026-08-27, later revision) —
  ESP-NOW only needs a matching radio channel, not an AP
  association/IP, so `SM_PROVISION_CHANNEL` hands over just a channel
  number (`SM_ChannelAssignment`), not SSID/password. This also means the
  pod has **no network OTA** (needs real IP/mDNS) - reflash over USB only.
  If a future feature needs the pod on the real network/internet, that's
  a separate opt-in Wi-Fi join, not something to fold back into channel
  provisioning.
- Encryption for the provisioning handshake (ESP-NOW per-peer AES,
  `SM_PROVISIONING_PMK`/`LMK`) was tried and **reverted** after real
  hardware testing showed it silently failed — the receiving side needs
  to have pre-registered the sender as an encrypted peer before the first
  encrypted packet arrives, which isn't possible for a provisioner/pod
  that have never talked before. Messages are plain again. Matters less
  now anyway since the payload is just a channel number.
- The CYD verifies pairing actually completed rather than assuming success
  once it sends the channel: `espnow_pair_outcome()` in `espnow_state.*`
  tracks whether the pod's `SM_PROVISION_ACK` came back (5s timeout), and
  `ui_shell.cpp`'s `poll_ack_dialog` shows a follow-up "Paired
  successfully" / "No confirmation" dialog after tapping Pair.
- A `SM_PROVISION_REQUEST` from a MAC already in the paired-bridges list
  is treated as "lost its channel, sweeping again" and auto-resent the
  channel immediately, no popup - see `espnow_state.cpp`'s
  `find_paired_bridge_index` check early in the `SM_PROVISION_REQUEST`
  handler.
- `SM_PING`/`SM_PONG` (0x14/0x15) added for a manual connectivity test:
  short button press on the pod (once provisioned) broadcasts
  `SM_PING`, the CYD replies `SM_PONG`, pod's LED flashes white on
  success or logs a 3s timeout.
- LVGL gotcha hit while building the pairing popup: `lv_msgbox_create`'s
  default width (`2*LV_DPI_DEF` = 260px here) and footer-button width
  (`LV_DPI_DEF/3` ≈ 43px) are tiny on an 800x480 panel - explicitly
  `lv_obj_set_width()`/`lv_obj_set_size()` the msgbox and its footer
  buttons (see `ui_shell.cpp`'s `poll_pair_request`), or taps land on the
  modal backdrop instead of the button and silently do nothing.
- The ESP NOW page (`ui_espnow.cpp`) split into a main page plus three
  sub-overlays as of 2026-08-27: **Setup** (friendly name, moved off the
  main page), **Monitor** (live traffic log from `espnow_state`'s
  `kMaxTrafficLogEntries`=40 ring buffer, logged generically in
  `send_to()`/`on_espnow_recv()` so it captures every message type
  regardless of whether the CYD acts on it; plus a "Send Test Ping"
  button), and **Diagnostics** (paired-bridge list with per-device Reboot /
  Restore / Forget buttons - Reboot/Restore unicast `SM_COMMAND`
  `"REBOOT"`/`"FACTORY_RESET"` to the pod via `espnow_send_command()`,
  Forget only removes the CYD's own record; a disabled "Update (soon)"
  button is a placeholder for a future firmware-over-ESP-NOW path,
  explicitly not implemented yet per user request). Known Devices stayed
  on the main page. Each sub-overlay follows the same
  hide-main/show-overlay pattern as the pre-existing Name-entry keyboard
  overlay.

## Session log

- **2026-08-27**: Added icons to the drawer's menu buttons
  (`make_drawer_btn` in `src/ui_shell.cpp`) using LVGL's built-in symbol
  font — Wi-Fi -> `LV_SYMBOL_WIFI`, Esp Now -> `LV_SYMBOL_BLUETOOTH`,
  Setup -> `LV_SYMBOL_SETTINGS`. No new image assets added. Both
  PlatformIO envs (`esp32-8048S043C`, `esp32-8048S043C-ota`) build clean.
- **2026-08-27**: Added Home tab mode tiles (FN Main / FN Output /
  Capture-Learn, all placeholder screens); removed the boot-splash Wi-Fi
  status text and the "ShackMate" wording on the ESP NOW page per
  feedback. Started the `M5AtomS3-FN-Bridge` companion project and added
  a first pass at ESP-NOW Wi-Fi provisioning with the CYD hopping to a
  fixed pairing channel to scan ("Pair FN Bridge" button/page) — verified
  reception working end-to-end on real hardware (fixed a channel-switch
  race, an AtomS3-Lite USB-CDC boot flag, and an LED pixel-count bug along
  the way).
- **2026-08-27 (later)**: Replaced that scan-based design per feedback —
  it required the CYD to leave its own Wi-Fi network, which was worse UX
  than having the pod do the searching. New model: the pod sweeps
  channels 1-11 broadcasting `SM_PROVISION_REQUEST`; the CYD stays put and
  auto-offers a "Pair?" popup (`ui_shell.cpp`) whenever it hears one; on
  accept it sends `SM_PROVISION_CREDENTIALS` (now ESP-NOW-encrypted,
  shared `SM_PROVISIONING_PMK`/`LMK`) and persists the pod in a 10-slot
  paired-bridges NVS list. Added `SM_PROVISION_HOLD`/`_REJECTED`/`_ACK`
  message types and a per-attempt nonce (`SM_ProvisionRequest`/
  `SM_ProvisionNonce`) so a human's slower confirmation doesn't race the
  pod's channel dwell. Renamed `SM_DEVICE_FN_BRIDGE` ->
  `SM_DEVICE_FN_2WIRE_POD`, added `firmwareVersionPatch` to
  `SM_DeviceIdentity`, pod's default friendly name is now `"FN-POD-01"`.
  Removed `ui_bridge_pairing.*` and the old scan API entirely. Both
  projects' PlatformIO envs build clean; not yet flashed/tested end-to-end
  on real hardware in this new form.
- **2026-08-27 (evening)**: Fixed real bugs found via hardware testing of
  the above: pairing dialog too small to reliably tap "Pair" (LVGL msgbox
  default sizing), "Forget" button on the ESP NOW page not refreshing the
  list, and — the big one — encrypted `SM_PROVISION_CHANNEL`/`_ACK`
  packets never actually reaching the pod (ESP-NOW encrypted-peer
  bootstrap doesn't work the way it was designed to; reverted to plain
  messages). Added ACK-based pairing verification (see "Companion
  project" bullets above), auto-reprovisioning for already-paired pods,
  `SM_PING`/`SM_PONG` connectivity test on the pod's button, a 5s
  hold-anytime factory-reset gesture (replacing the old boot-only one),
  and a blinking LED while the pod sweeps. Then redesigned channel
  hand-off to not use Wi-Fi at all (pod never joins an AP, no IP, no
  OTA) per a design reconsideration mid-session - see "Companion
  project" bullets. Both projects rebuilt clean after every change; last
  redesign (no-Wi-Fi pod) not yet flashed/tested on hardware.
- **2026-08-27 (night)**: Added the ESP NOW page's Setup/Monitor/
  Diagnostics split (see "LVGL notes" bullet above) plus the backend it
  depends on: a generic ESP-NOW traffic-log ring buffer
  (`espnow_state.cpp`'s `log_traffic()`, called from both `send_to()` and
  `on_espnow_recv()`), `espnow_send_test_ping()`, and
  `espnow_send_command()`. On the pod side, added an `SM_COMMAND` handler
  (`"REBOOT"`/`"FACTORY_RESET"`, always ACKed first), `WIFI_PS_MIN_MODEM`
  power-save once provisioned (benefit unconfirmed without an AP's DTIM
  interval - see the pod's README), and simplified the LED scheme per
  user's explicit choice: green = unprovisioned/sweeping, solid blue = any
  provisioned state (collapsing the old blue-idle/green-peer-seen split,
  since the pod should be reachable for remote commands regardless of
  recent traffic). Firmware-update-over-ESP-NOW was explicitly scoped as
  future-only (a disabled "Update" button placeholder exists on
  Diagnostics, no implementation). Both projects rebuilt clean
  (`esp32-8048S043C`, `esp32-8048S043C-ota`, `m5stack-atoms3`); not yet
  reflashed/tested on hardware.
- **2026-08-27 (night, cont'd)**: Flashed both projects to real hardware
  and forced a CYD reset (RTS toggle via a raw pyserial script, since
  `platformio.ini` disables `monitor_rts`/`monitor_dtr` for normal
  `pio device monitor` use) to finally capture a clean boot log. Confirmed
  the previously-unresolved "shows known device, but not paired bridge"
  report is **not** an NVS persistence bug - boot log showed
  `[paired-bridges] loaded 1 entry from NVS (raw bytes=40,
  sizeof(SM_PairedBridge)=40)` with the correct MAC/name
  (`48:CA:43:B6:4B:D8 "FN-POD-01"`) loading fine. The diagnostic
  `Serial.printf`s added to `load_paired_bridges()`/`save_paired_bridges()`
  can stay or be trimmed later; the underlying data path works.
- **2026-08-27 (night, cont'd)**: Hardware testing surfaced real ESP-NOW
  reliability problems (SM_PING rarely ACKed, the pod's button press
  usually not seen by the CYD) - root-caused to the CYD never disabling
  Wi-Fi modem-sleep (`app_state.cpp`'s `app_wifi_apply()`), a well-known
  ESP-NOW+WiFi coexistence gotcha: default STA power save only wakes the
  radio on the AP's DTIM schedule, delaying/dropping ESP-NOW frames -
  worse still given the CYD's Wi-Fi is currently failing to associate at
  all (`ASSOC_FAIL` reason 203, seen on every single boot log captured
  this session - likely a stale/wrong SSID or password, needs checking on
  the Wi-Fi setup page). Fixed by adding `WiFi.setSleep(false)`. Also
  reverted the Atom pod's `WIFI_PS_MIN_MODEM` power-save added earlier
  this same session - its battery benefit was already flagged as
  unconfirmed, and the user prioritized real-time responsiveness (this
  pod exists to control hardware) over an uncertain saving. Removed the
  "Known Devices" list from the ESP NOW page and the Home tab's Wi-Fi
  status label per feedback (redundant with the top-bar Wi-Fi icon).
  Confirmed via the pod's own serial log that heartbeats genuinely go out
  every 10s as designed - a report of "heartbeat every 1 second" was very
  likely just the Monitor page's 1s age-label refresh being misread as
  new traffic, not an actual firmware bug.
- **2026-08-27 (night, cont'd further)**: Root-caused the CYD's "Send Test
  Ping" getting no reply at all (while the pod's own button-press ping
  seemed to work): both sides' `on_ping()`/SM_PING handler called
  `send_to(mac, SM_PONG, ...)` **without** `ensure_peer(mac)` first -
  `esp_now_send()` silently fails to a MAC not in the local peer table,
  and neither side re-registers the other as a peer on a normal
  already-provisioned boot (only the pairing handshake and SM_COMMAND
  handling did that). Fixed by adding `ensure_peer(mac)` before the PONG
  reply on both `espnow_state.cpp` and the pod's `main.cpp`. Also added a
  brief (60ms) per-message LED flicker on the pod - red while
  transmitting, green while receiving - as a visual debug aid, generic
  across every message type via `send_to()`/`on_espnow_recv()` but
  deliberately excluding `SM_HEARTBEAT` (its constant 10s drumbeat would
  drown out the flicker's usefulness for spotting real ping/command
  activity) per explicit request. Sits below the existing ping-success
  white flash in `update_led()`'s priority order.
- **2026-08-27 (night, cont'd yet further)**: Fixed the Diagnostics
  "card" rows rendering invisible/white-on-white (missing
  `lv_obj_remove_style_all()` left the default theme's light panel
  background behind white label text - every other list row in this app
  strips that, this one didn't). Bottom-centered the Setup/Monitor/
  Diagnostics nav row via a `flex_grow`-1 spacer pushing it down the
  overlay's flex column. Fixed a real "Forget doesn't stick" bug:
  `save_paired_bridges()` called `Preferences::putBytes(key, data, 0)`
  when the list emptied, which doesn't reliably clear an existing NVS
  key - it can no-op, leaving the pre-Forget blob to reload on the next
  boot (our own success logging masked this by hardcoding `ok=true`
  whenever count was 0). Now calls `prefs.remove(key)` instead when
  empty. Also: `espnow_forget_paired_bridge()` now sends the pod a
  best-effort `SM_COMMAND "FACTORY_RESET"` before removing it - ESP-NOW
  is connectionless, so without this the pod never learned it was
  forgotten and just kept broadcasting/replying on its saved channel
  exactly as before, which is why it looked like it "still connected"
  after being forgotten on the CYD side. Same single-fire-and-forget-
  packet unreliability turned up a second time in the pairing handshake
  itself: the pod's `on_provision_channel()` sent `SM_PROVISION_ACK`
  exactly once, then rebooted ~200ms later - real testing showed pairing
  itself always working (fast, functional) while the CYD's ACK-based
  confirmation dialog always timed out, meaning that one packet
  consistently got lost right as the radio was about to be torn down.
  Fixed the same way as `espnow_forget_paired_bridge()`: send the ACK 4x,
  20ms apart, before the delay+restart (harmless duplicates, since the
  CYD's ACK handling just needs one with the right nonce/MAC to land).
  General lesson for this protocol: any single-shot ESP-NOW send that
  matters (especially right before a restart) should be sent a few times
  rather than once - `espnow_send_command()`/`send_to()` themselves stay
  single-shot; the retry loop is the caller's responsibility per call
  site, added only where it's mattered in practice so far.
- **2026-08-27 (still night)**: The 4x-ACK-retry fix above didn't fully
  solve it - real hardware testing still showed "pod pairs instantly, CYD
  says no confirmation." Root cause was a race, not packet loss:
  `espnow_accept_pair_request()` (`espnow_state.cpp`) sent
  `SM_PROVISION_CHANNEL` and then called `add_or_update_paired_bridge()`
  - which does a synchronous NVS flash write - *before* arming
  `s_pair_outcome = SM_PAIR_WAITING`/`s_awaiting_ack_mac`/`nonce`. The pod
  does its own NVS write (`save_channel()`) then fires its ACK retries
  almost immediately, with no other wait - comparable timing to the CYD's
  own flash write (ESP32 NVS writes commonly run 10-50ms+ and can stall
  interrupt-driven ESP-NOW receive processing for their duration). If the
  pod's ACK arrived while the CYD was still mid-write, `s_pair_outcome`
  was still `SM_PAIR_NONE`, so the ACK handler's match check failed and
  the ACK was silently dropped even though it genuinely arrived. Fixed by
  reordering: arm the awaiting-ACK state immediately after
  `send_to(..., SM_PROVISION_CHANNEL, ...)`, before the NVS write. Not yet
  re-flashed/tested on hardware after this reorder.
- **2026-08-27 (later still)**: The NVS-ordering fix above did **not**
  actually fix it - reflashed and retested, same "no confirmation"
  result, even though the pod visibly paired and Diagnostics showed it.
  Captured live serial logs from both boards simultaneously (raw pyserial
  script, since `pio device monitor` needs a real TTY and fails
  non-interactively - `dtr=False`/`rts=False` set before `open()` to avoid
  an unwanted reset, though the CH340/native-USB-CDC ports on both boards
  still reset on open regardless) and found the CYD's log **never once**
  printed `<- SM_PROVISION_ACK`, despite the pod's own log confirming it
  ran its full save+ACK+restart path and successfully rejoined. So the
  ACK genuinely isn't reaching the CYD's radio - not a logic/race bug.
  Root cause: the CYD's boot log shows `Reason: 203 - ASSOC_FAIL` on
  **every single boot** this session (also previously flagged, unfixed,
  in the 2026-08-27 night entry above, and already anticipated in a
  `main.cpp` comment near the watchdog init: "repeated ASSOC_FAIL against
  a stale saved password"). A STA that can't associate keeps
  background-retrying via Arduino-ESP32's Wi-Fi driver, which involves
  scanning across channels - since ESP-NOW shares that same radio and
  only receives on whatever channel it's currently tuned to, those scans
  can carry it off the pairing channel for a stretch. The pod's ACK burst
  was only ~100ms wide (4x/20ms) - easily swallowed entirely by one such
  gap - while other traffic (10s heartbeats, pings) gets many chances
  and mostly gets through, which is why *only* the ACK looked broken.
  **Two fixes, different owners:** (1) the CYD's saved Wi-Fi
  SSID/password need to be corrected via its Wi-Fi setup page so the STA
  actually associates - this is on the user, not fixable from source; (2)
  regardless, widened the pod's `on_provision_channel()` ACK retries from
  a tight 4x/20ms burst to 8 sends spread over ~3.2s with backoff
  (`M5AtomS3-FN-Bridge/src/main.cpp`), comfortably under the CYD's 5s
  `kAckTimeoutMs`, so there are more/wider-spaced chances to land while
  the CYD's radio happens to be back on the right channel even with
  Wi-Fi still unhealthy. Both projects rebuilt and reflashed; result of
  this retest not yet known as of this entry.
- **2026-08-27 (later still, correction)**: The ASSOC_FAIL/channel-scanning
  theory above was a wrong turn - user pushed back that Wi-Fi is fine on
  their network (this session's earlier ASSOC_FAIL boots were apparently
  just a stale-password fluke, not the real network's normal state) and
  that the actual pairing handshake completes fine (pod reboots and shows
  paired in Diagnostics); only the CYD's own confirmation dialog is wrong.
  Also reported the widened-ACK-retry change **broke** something new:
  the pod stopped un-pairing when using Diagnostics' "Forget" - almost
  certainly because that change made `on_provision_channel()` block for
  up to ~3.2s inside a tight retry loop, and a `FACTORY_RESET` command
  arriving from a same-session Forget click during that window had no
  chance to be processed before the pod's own `ESP.restart()` fired.
  **Reverted** the ACK-retry-window widening entirely (back to the
  original 4x/20ms burst) per explicit request.
  Re-examined the same serial capture with fresher eyes and found the
  real bug, unrelated to Wi-Fi association health: partway through the
  CYD's log, `[W][WiFiGeneric.cpp] Reason: 8 - ASSOC_LEAVE` (a
  **client-initiated** disassociation - i.e. the CYD's own code called
  `WiFi.disconnect(true)`, most likely via the top-bar Wi-Fi icon's
  tap-to-disconnect gesture in `ui_shell.cpp`) is immediately followed by
  a permanent flood of ESP-IDF's own `E ESPNOW: esp now not init!`
  errors for the rest of that boot. Root cause: `app_wifi_apply()`
  (`app_state.cpp`) opens with `WiFi.disconnect(true)` (a full radio
  stop, not just a disassociate) then `WiFi.mode(WIFI_STA)` to bring it
  back - and on real hardware this silently deinitializes ESP-NOW as a
  side effect (peer table and receive callback gone too), with nothing in
  the Wi-Fi driver notifying `espnow_state.cpp` that it happened.
  `espnow_init()`/`espnow_start_messaging()` only ever ran once, at boot,
  in `main.cpp` - so once *anything* triggers a second `app_wifi_apply()`
  call in the same boot (the Wi-Fi icon's tap-to-disconnect or
  long-press-to-reconnect gestures in `ui_shell.cpp`, or saving new
  credentials via `ui_wifi_setup.cpp`'s `connect_and_remember()`), every
  `esp_now_send()` afterward silently no-ops for the rest of that boot -
  explaining both symptoms at once: a pairing confirmation (or a later
  Forget's `FACTORY_RESET`) that never arrives, while Diagnostics still
  correctly shows "paired" because `add_or_update_paired_bridge()` writes
  the CYD's own local list unconditionally, without checking whether the
  underlying send actually succeeded.
  **Fix**: added `espnow_reestablish_after_wifi_change()`
  (`espnow_state.h`/`.cpp`) - re-runs `esp_now_init()` (harmlessly a
  no-op via `ESP_ERR_ESPNOW_EXIST` if ESP-NOW was never actually
  knocked out), re-adds the broadcast peer if missing, and
  re-registers the receive callback; no-ops entirely if `espnow_init()`
  hasn't run yet (`s_enabled` still false), so it's safe to call
  unconditionally. Wired into `app_wifi_apply()` itself
  (`app_state.cpp`) right after `WiFi.mode(WIFI_STA)`/`setSleep(false)`,
  covering every call site (boot, Wi-Fi icon, Wi-Fi setup page) from one
  place. Paired-bridge peers aren't explicitly re-added - `ensure_peer()`
  already re-adds any peer lazily on next send. Both projects rebuilt
  clean; CYD reflashed with this fix, pod reflashed back to its reverted
  (original 4x/20ms ACK) firmware. Not yet retested on hardware as of
  this entry.
- **2026-08-27 (finally resolved)**: The `espnow_reestablish_after_wifi_change()`
  fix above didn't solve it either - real hardware retesting (with live
  serial capture from both boards running simultaneously via a raw
  pyserial script, since `pio device monitor` needs a real TTY and can't
  run non-interactively) kept showing the exact same "pod pairs fine, CYD
  says no confirmation" result. Found and fixed three more real bugs in
  the pod's `on_provision_channel()`/`loop()` before finally landing on
  the actual root cause:
  1. **Hold-timeout race.** `on_provision_channel()` runs from the
     ESP-NOW receive-callback context, not `loop()` - so `loop()`'s own
     30s hold-timeout check kept running concurrently the whole time.
     Since every test in this debugging session took longer than 30s
     between the pod going cyan and a human actually tapping "Pair" (this
     conversation's own back-and-forth pacing), `loop()`'s timeout fired
     *during* the ACK-send sequence almost every time and called
     `esp_wifi_set_channel()` to resume sweeping - physically moving the
     pod's radio off-channel while it was still trying to transmit the
     ACK. Confirmed via `Hold timed out with no decision - resuming
     sweep.` printing interleaved with the ACK-attempt logs. First fix
     attempt (just clearing `s_holding` early) was incomplete - `loop()`
     has a *separate* dwell-based hop path, independent of the timeout
     check, that fires as soon as `s_holding` is false and enough time
     has passed since the last hop (always true by then). Real fix: a
     dedicated `s_channel_committed` flag that shuts down `loop()`'s
     entire unprovisioned sweep/hold block outright (a proper three-way
     branch: committing / sweeping / provisioned - not a two-way one),
     set at the very top of `on_provision_channel()`.
  2. **Stale MAC pointer.** `esp_now_recv_cb_t`'s `mac` pointer is only
     guaranteed valid for one callback invocation - it points into the
     WiFi driver's own receive buffer, which can be reused by other
     incoming traffic at any time. `on_provision_channel()` used to hold
     onto that raw pointer and reuse it across a ~600ms span with several
     `delay()`s in between, risking every send after the first firing at
     a stale/corrupted address. Fixed by copying the MAC into a local
     `uint8_t[6]` immediately and using only that copy.
  3. **Queue contention from an incomplete guard.** The first attempt at
     fix #1's `s_channel_committed` flag used `if (!s_provisioned &&
     !s_channel_committed) {sweep} else {heartbeat/etc.}` - but
     `s_provisioned` is *still false* while a channel is being committed
     (it only flips true on the *next* boot), so that fell through to the
     heartbeat/ping-timeout branch instead of doing nothing. Confirmed on
     hardware: an extra `send_heartbeat()` broadcast fired mid-ACK-burst,
     and one ACK attempt outright failed right after - almost certainly
     that concurrent traffic contending for ESP-NOW's small internal send
     queue. Fixed with a real three-way branch (`if (s_channel_committed)
     {nothing} else if (!s_provisioned) {sweep} else {heartbeat/etc.}`).
  With all three fixed, the CYD *still* never received `SM_PROVISION_ACK`
  - confirmed via `esp_now_register_send_cb()` diagnostic logging on the
  pod that its own async delivery-confirmation callback simply never
  fired for this specific unicast send, no matter what else was tried
  (explicit peer channel instead of channel-0 auto-resolve, sending
  before vs. after the pod's own `save_channel()` NVS write, a single
  send instead of a 4x burst) - while every broadcast this pod has ever
  sent, all session, got an immediate confirmed `SUCCESS`, and a manual
  "Send Test Ping" round trip proved unicast pod→CYD delivery *does* work
  fine from a calm, steady-state call context (the `SM_PING`/`SM_PONG`
  path). So the failure is specific to sending unicast synchronously from
  within `on_provision_channel()`'s particular call context (reached
  directly from the ESP-NOW receive callback for the packet that
  triggered it) - true root cause not identified at the ESP-NOW/driver
  level, and not worth continuing to chase.
  **Final fix - architectural, not another timing patch**: stopped
  depending on `SM_PROVISION_ACK` as the CYD's primary confirmation
  signal at all. The pod's `enter_joined_mode()` now calls
  `send_announce()` (broadcast) immediately after `send_discover()` upon
  rejoining on its new channel; the CYD's `on_espnow_recv()` `SM_ANNOUNCE`
  case now checks `identity.deviceID` against a new
  `s_awaiting_ack_deviceID` (set alongside the existing
  `s_awaiting_ack_mac`/`nonce` in `espnow_accept_pair_request()`) and
  marks `SM_PAIR_CONFIRMED` on a match. This reuses the *reliable*
  broadcast path instead of the unreliable unicast one. The
  `SM_PROVISION_ACK` burst is kept as a best-effort secondary signal
  (harmless if it happens to land) but the CYD no longer depends on it.
  Confirmed working on real hardware after this change - user reported
  "Paired successfully now," and the pod's log showed `-> SM_ANNOUNCE`
  with a confirmed broadcast `send_cb` `SUCCESS` right after rejoining.
  Removed the temporary `esp_now_register_send_cb()` diagnostic logging
  from the pod afterward. `ESPNOW_PROTOCOL.md`'s "Verification" section
  updated to describe this. **General lesson**: when a specific
  send/receive path in this protocol proves stubbornly unreliable despite
  ruling out every timing/ordering bug found, prefer redesigning around
  an already-proven-reliable path (broadcast, in this protocol's case)
  over continuing to patch the unreliable one - especially once multiple
  real, unrelated bugs have already been found and fixed along the way
  without resolving the core symptom.
- **2026-08-27 (same day, FN protocol work)**: Pivoted from ESP-NOW
  debugging to the actual FN_OUTPUT_Tester_Handoff work - decoding
  `captures/digital.csv` and building a pod-side replay. Full detail in
  that package's `docs/EXPERIMENT_LOG.md` (Experiment 002) and
  `docs/FN_PROTOCOL_FINDINGS.md`, but the headline results, since they
  affect this repo too:
  - Re-analysis (autocorrelation + cross-correlation, `saleae/analysis/`
    scripts, needs a scratch numpy venv) found the true frame period is
    **~50ms** (3 mains cycles), not the ~16.7ms originally recorded, and
    that this specific capture's frame content is **static/repeating**
    (93-97% match across 11 consecutive frames) - consistent with it
    being a fixed idle/keepalive frame, matching the operator's report
    that replaying it satisfies a real OUTPUT board's watchdog and clears
    its alarm. Still no bit-level protocol decode - that needs a capture
    with a deliberate commanded change, not available this session.
  - Built `M5AtomS3-FN-Bridge/src/fn_bus_tx.{h,cpp}` - an RMT-driven
    replay of the extracted reference frame on the pod's GPIO2 (Grove
    G1), controlled over Serial (`tx`/`stop` commands), explicitly
    bench-test-only (bare GPIO, no isolation/protection circuitry exists
    - do not connect to the real FN bus). Hit and worked through two real
    ESP32-S3 hardware/driver issues along the way: a FreeRTOS task stack
    overflow (fixed: 8192 bytes), and a genuine crash in the legacy
    `driver/rmt.h` compat layer's interrupt-refill path for
    larger-than-hardware-buffer transmissions (`RMT_EVT_THRESH_ERR` /
    `LoadProhibited` panic - this Arduino core release doesn't ship the
    modern `rmt_tx.h` driver at all, confirmed absent in every other
    cached core version too, so that rewrite wasn't available as an
    option this session). Resolved by noise-filtering the reference frame
    (merging edges <3.2µs into their neighbors, 610→386 edges) to fit
    ESP32-S3's confirmed hard limit of 256 RMT words per channel (only 4
    TX channels x 64 words each), and using true hardware auto-loop mode
    (`rmtLoop()`) instead of software-refilled one-shot writes - confirmed
    stable via repeated start/stop/restart cycles on real hardware with
    no crashes, ESP-NOW mesh functionality unaffected throughout.
  - **Not yet done**: visually verifying the pod's actual GPIO2 output
    against the original capture with a real scope/logic analyzer -
    nothing this session confirmed the replayed waveform is correct
    beyond "the firmware runs without crashing." Do this before trusting
    it for anything beyond a rough bench check.
  - Debugging workflow note for future sessions: `pio device monitor`
    doesn't work non-interactively in this environment - used a raw
    pyserial script instead (`dtr=False`/`rts=False` before `.open()`,
    background-loggable via `nohup ... & disown`). Both the CYD's CH340
    port and the pod's native-USB-CDC port reset the board on every
    serial open - expected, not a bug, and it wiped in-progress ESP-NOW
    pairing state more than once mid-session as a result.
- **2026-09-01**: Rebuilt `ui_fn_output.cpp`/`.h` from the old bench-test-only
  Start/Stop/UART-Test screen into the real Output Tester: a persisted
  (`AppConfig::fn_output_model`, NVS key `fn_model`) PCB-110 (10 outputs, no
  analog) / PCB-085 (16 outputs + 4-20mA analog) model picker, an "Outputs
  Enabled" switch (reuses the existing `FN_TX_START`/`FN_TX_STOP` commands),
  a pre-allocated 16-slot grid of checkable "LED" tiles (extras hidden per
  model) that toggle on tap, an analog 0-100% slider (PCB-085 only, shown/
  hidden via `apply_model_to_ui()`) with a live mA readout, and an "All
  Outputs Off" action - all following this project's existing pre-allocated-
  pool/fire-and-forget-SM_COMMAND conventions. Added four new SM_COMMAND
  names sent from this screen: `SET_MODEL` (argument 0/1), `SET_OUTPUT`
  (argument bit0 = state, bits1-5 = 0-based index), `SET_ANALOG` (argument
  0-100), `ALL_OUTPUTS_OFF` - documented on `espnow_state.h`'s
  `espnow_send_command()`, which gained an `argument` parameter (default 0,
  so existing Diagnostics call sites didn't need changes) since
  `SM_CommandPayload.argument` existed in the wire format but was never
  actually wired up before. Mirrored handling into
  `../M5AtomS3-FN-Bridge/src/main.cpp`'s `on_command()` - the pod now tracks
  `s_fn_output_model`/`s_fn_output_state[16]`/`s_fn_analog_percent` and ACKs
  these commands, but **does not yet fold them into the transmitted FN
  waveform** - `fn_bus_tx` still just loops the fixed idle reference frame
  regardless of this state. Building a real per-model FN frame encoder
  (PCB-110/PCB-085 address+data generation reflecting live output/analog
  state, per `FN_OUTPUT_Tester_Handoff/CLAUDE.md`'s "Continuous State
  Requirement") is unstarted, separate follow-up work - this session was
  scoped to the CYD UI + transport plumbing only, per explicit user
  direction (confirmed via `AskUserQuestion` before starting: wire the
  ESP-NOW commands up now rather than build CYD-UI-only). The operator
  initially specified PCB-110 as an 8-output board, which conflicted with
  `docs/PCB110_ANALYSIS.md`'s existing "10-solid-state-output" line - flagged
  there as a contradiction rather than silently overwritten - then corrected
  it to 10 minutes later, matching the doc; fixed everywhere (model table,
  header comments, pod's variable comment/log string) and the doc's
  contradiction note reverted to a short resolved-note. Both `CYD-4.3-FN-Tester` PlatformIO
  envs (`esp32-8048S043C`, `esp32-8048S043C-ota`) and
  `M5AtomS3-FN-Bridge`'s `m5stack-atoms3` env build clean; not yet flashed/
  tested on real hardware this session. Also answered a question (no code
  change): there's no drop-in emulator for this exact board (800x480
  RGB-parallel LovyanGFX + GT911 touch) - Wokwi's CYD support only covers
  the smaller SPI/resistive-touch variants. A native LVGL+SDL2 PlatformIO
  env is possible but would need the WiFi/ESP-NOW/Preferences calls this
  UI touches directly stubbed out first - scoped as a separate future
  effort, not started.
- **2026-09-01 (later)**: Built out `ui_capture_learn.cpp`/`.h` (previously a
  "Not implemented yet" stub) into a real Capture/Learn screen, per user
  request to add the same PCB-110 (10 OUT)/PCB-085 (16 OUT + analog) model
  toggle used on the FN Output screen. First factored the model
  enum/table/output-count constant that both screens now share out of
  `ui_fn_output.cpp` into a new `src/fn_output_model.h` (`FnOutputModel`,
  `FnModelInfo`, `kFnModels`, `kFnMaxOutputs`, `fn_output_model_clamped()`) -
  avoids two copies of evidence-linked data (output counts, analog
  capability) drifting apart, and both screens now read/write the same
  `AppConfig::fn_output_model`, so picking the board once applies everywhere.
  Capture/Learn itself is deliberately **not** a copy of the Output Tester's
  command grid - per `FN_OUTPUT_Tester_Handoff/CLAUDE.md`'s "CAPTURE / LEARN
  Mode" section, this mode is about *observing*, not commanding, and this
  tester has no FN-MAIN receive interface built yet (TESTER_ARCHITECTURE.md
  development-order items 6-7 are still unbuilt) - so it can't decode live
  FN traffic on-device. Built instead as a manual field-notebook: a main
  page (model picker + a reference panel showing the known PCB-085 address
  map from docs/PCB085_ANALYSIS.md, or a "no confirmed map yet" note for
  PCB-110) plus two sub-pages - "Mark Outputs" (a touch grid visually like
  the Output Tester's LEDs, but tapping one only writes a local timestamped
  log entry, nothing is transmitted; also an observed-analog slider +
  "Log Value" for PCB-085, and "All Marked Off") and "Session Log" (a
  Saleae-capture-filename field via the same dedicated-keyboard-overlay
  pattern as the ESP NOW page's Name field, a scrollable elapsed-time-stamped
  log list styled after the ESP NOW Monitor page's ring buffer, "Clear Log",
  and "Save to SD" - writes `/capture_learn/session_NNN.csv` on the SD card,
  auto-picking the first unused NNN so it never overwrites an earlier
  session, header row carries the model + the entered Saleae filename so the
  CSV stays correlated with that external evidence). Both CYD PlatformIO
  envs build clean; not yet exercised on real hardware (including whether
  the SD card is actually present/working on the bench unit) this session.
- **2026-09-01 (later still)**: Added a persisted, structured "Address
  Banks" knowledge base per user request ("simulate the output by Name,
  Output #, and correct Bank Address and Data" - i.e. capture the actual
  reusable protocol mapping, not just a session log). New `src/fn_bank_profile.h`/
  `.cpp` module (deliberately separate from `ui_capture_learn.cpp` - per
  `CLAUDE.md`'s "keep protocol handling separate from UI code" - this owns
  the data model, not the touchscreen). The data model matches FN's actual
  confirmed shape (`docs/PCB085_ANALYSIS.md`): a 5-bit address selects a
  **bank**, and a bank is either `FN_BANK_DIGITAL` (4 independently-named
  outputs, one per D1-D4 bit - e.g. address 10001 -> Alarm/Valve1/Valve2/
  ProcessBlower), `FN_BANK_ANALOG` (the whole 4-bit field is one combined
  value, e.g. 10110's 0-15 code - only slot 0 used), or `FN_BANK_UNKNOWN`
  (address seen, meaning not yet determined). This is a materially more
  correct model than "address + output name" pairs would have been - a
  single output isn't self-contained, it's one bit within a 4-output bank.
  Each slot carries its own `FnConfidence` (UNKNOWN/HYPOTHESIS/STRONG/
  CONFIRMED, matching the project's existing evidence-discipline vocabulary).
  PCB-085 seeds with the 4 already-CONFIRMED/STRONG-EVIDENCE banks
  (10001, 10010, 10110, 10100) transcribed from the docs; PCB-110 seeds
  empty (no confirmed output correlation exists for it yet - this whole
  profile is meant to be discovered from scratch via this screen).
  Deliberately did NOT seed PCB-085's unassigned 10000/10011 addresses -
  per the doc's "do not pre-suggest unverified output assignments" rule,
  an operator adds those via "Add Bank" only once actually captured.
  Persisted per-model to `/board_profiles/pcb110.csv` /
  `/board_profiles/pcb085.csv` on the SD card ('|'-delimited, since names
  are free text and could contain commas) - unlike the Session Log's
  never-overwrite files, saving a profile **does** overwrite the previous
  one, since this is a curated/edited profile, not raw capture evidence.
  UI: a third Capture/Learn nav button "Address Banks" opens a scrollable
  bank-summary list ("10001 [DIGITAL] Alarm | Valve 1 | Valve 2 | Process
  Blower") with Add Bank / Save Profile; tapping a row (or a newly-added
  bank) opens an editor sub-page (kind selector, 4 slot rows each with a
  tap-to-name field and a tap-to-cycle confidence badge, Delete Bank).
  Refactored the Saleae-filename keyboard overlay (previously dedicated to
  that one field) into a single shared, reusable text-entry overlay
  (`TextEntryTarget` enum dispatch) since this added five more free-text
  fields (new-bank address, 4 slot names) that all needed the same
  full-page-keyboard pattern - avoids five more near-duplicate overlays.
  Both CYD PlatformIO envs build clean (one truncation warning on the bank
  summary snprintf, fixed by widening the buffer); not yet exercised on
  real hardware. **Not yet done, flagged as the obvious next step**: nothing
  actually *consumes* this profile yet - the FN Output screen's "SET_OUTPUT"
  command still just sends a bare output index/state to the pod (see that
  screen's own entry above), and the pod still only loops a fixed reference
  frame. Wiring "look up Output N's bank+bit in this profile, encode that
  bit into the bank's FN frame" is the real encoder work this profile was
  built to eventually enable, and is still unstarted.
