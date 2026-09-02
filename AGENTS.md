# RTS FN Tester - Agent Instructions

Before modifying code, read:

- docs/PCB085_PROTOCOL.md

This project uses:

- M5Stack ATOM as the time-critical FN 2-wire/fiber interface pod
- ESP-NOW between ATOM and CYD
- CYD touchscreen as the user interface
- ESP32 RMT or equivalent hardware timing for FN TX/RX

Do not replace the decoded PCB-085 protocol with guessed values or generic
MC145026/MC145027 assumptions.

The Saleae-derived mappings in docs/PCB085_PROTOCOL.md are the source of truth.

Keep protocol handling separate from UI code.

Preferred structure:

- protocol/       FN encode/decode
- profiles/       PCB-085 and future board profiles
- transport/      ESP-NOW
- hardware/       RMT, GPIO, interface pod
- ui/             CYD touchscreen

For PCB-085:
- maintain complete output state
- continuously transmit required FN frames
- do not implement outputs as one-shot commands
- support simultaneous outputs
- analog command is part of the FN stream