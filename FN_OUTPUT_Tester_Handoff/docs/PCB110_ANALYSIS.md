# UNA-DYN PCB-110 Hardware Analysis

## Board
UNA-DYN PCB-110, approximately 1996 generation; older 10-solid-state-output board.

(2026-09-01: briefly flagged a contradiction here after the operator said 8
outputs while scoping the CYD-4.3-FN-Tester's FN Output screen - operator
subsequently corrected that to confirm 10, matching this doc. No re-trace
was performed; still resting on this doc's original figure, not a fresh
measurement.)

## Components
### U1 — MC145027P
**CONFIRMED**
- Pin 9 = DIN.
- Pin 9 connects to center pin of FO/Wire selector.

### Second MC145027P
Present. Exact role remains to be traced.

### U7
Appears to be a 74HC14 Schmitt-trigger inverter.

**STRONG EVIDENCE**
- U7 pin 2 connects to selector position 3.
- This appears to be the conditioned wired receive signal.
- U7 pin 7 drives a board-silkscreened "COMM OK" LED (per 2026-08-27 capture session, `docs/FN_PROTOCOL_FINDINGS.md`).

**CONTRADICTION — flagged, not resolved**
On a standard 74HC14 hex Schmitt-trigger inverter (DIP-14), pin 7 is GND, not a usable logic output — it cannot drive an LED. This conflicts with the "U7 pin 7 = COMM OK LED drive" report. Possible explanations, none yet confirmed:
- U7 is not actually a 74HC14 (the "STRONG EVIDENCE" identification above may be wrong).
- "Pin 7" as reported refers to a connector/header or LED-driver pin number, not the U7 IC's own pin 7.
- A different IC than U7 actually drives the LED, and the association with U7 was a mislabel during the bench session.
Needs a direct continuity check / photo of U7's markings and the LED's actual driving pin before this is resolved either way.

### U5 / Input Conditioning
Wired input appears to pass through isolation/conditioning involving U5 before U7. Exact identity/path remains to be reconstructed.

## Working Selector Topology
```text
Fiber receive path -------- selector position 1
                                  \
                                   >--- center ---> U1 pin 9 DIN
                                  /
Wired conditioned path ---- selector position 3
                            |
                            +---- U7 pin 2
```

## Working Wired Receive Path
```text
FN two-wire input
      |
protection / isolation / conditioning
      |
     U5 ?
      |
U7 Schmitt/inverter stage
      |
 U7 pin 2
      |
FO/Wire selector
      |
MC145027 U1 pin 9 DIN
```

## Measurements Needed
- continuity from field connector through U5
- U5 part marking/datasheet
- U7 full marking
- waveform at field input, before U7, U7 pin 2, and U1 pin 9
- inversion/polarity through stages
- selector orientation/pin numbering
- second MC145027 inputs/outputs
- decoder-to-10-output mapping

Photos and measurements take precedence over typical-circuit assumptions.
