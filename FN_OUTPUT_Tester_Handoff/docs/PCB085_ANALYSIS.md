# PCB-085 FN OUTPUT Board Analysis

## Purpose

This document records hardware and protocol findings specifically for the
UNA-DYN/FN PCB-085 OUTPUT board.

General FN transport/timing information belongs in:

    FN_PROTOCOL_FINDINGS.md

This document defines how PCB-085 interprets FN addresses and data.

---

# Confidence Definitions

## CONFIRMED
Directly measured, traced, repeatedly demonstrated, or independently validated.

## STRONG EVIDENCE
Strongly supported but requiring additional confirmation.

## HYPOTHESIS
Plausible interpretation requiring testing.

## UNKNOWN
Not yet determined.

---

# 1. Board Capabilities

## CONFIRMED

PCB-085 provides:

- 16 discrete outputs
- one 4–20 mA analog output
- FN communications input
- fiber-optic / 2-wire communications capability

Command/control information for both the discrete outputs and analog output is
carried through the FN communications stream.

Loss of valid FN communications causes commanded outputs to turn OFF.

---

# 2. Major Components

## CONFIRMED / visually identified

PCB-085 contains five MC145027 decoder ICs.

The analog section includes:

- AD694 4–20 mA transmitter
- AD620 instrumentation amplifier
- calibration/scaling components
- associated FN decoding logic

The presence of five MC145027 devices is an important hardware clue.

However, six recurring FN addresses have been observed.

Therefore:

**Do not assume a simple one-address-per-MC145027 relationship until the
hardware has been completely traced.**

---

# 3. Physical Output Mapping

## CONFIRMED

PCB output numbering runs right-to-left.

| Output | PCB LED | Known Function |
|---:|---|---|
| 1 | DS12 | Alarm |
| 2 | DS11 | Valve 1 |
| 3 | DS10 | Valve 2 |
| 4 | DS9 | Process Blower |
| 5 | DS13 | Regen Blower |
| 6 | DS14 | Regen Heater |
| 7 | DS15 | Isolation Valve |
| 8 | DS16 | Process Heater |
| 9 | DS4 | UNKNOWN |
| 10 | DS3 | UNKNOWN |
| 11 | DS2 | UNKNOWN |
| 12 | DS1 | UNKNOWN |
| 13 | DS5 | UNKNOWN |
| 14 | DS6 | UNKNOWN |
| 15 | DS7 | UNKNOWN |
| 16 | DS8 | UNKNOWN |

Physical four-output groups are therefore:

### Group 1

    Output 1 = DS12
    Output 2 = DS11
    Output 3 = DS10
    Output 4 = DS9

### Group 2

    Output 5 = DS13
    Output 6 = DS14
    Output 7 = DS15
    Output 8 = DS16

### Group 3

    Output 9  = DS4
    Output 10 = DS3
    Output 11 = DS2
    Output 12 = DS1

### Group 4

    Output 13 = DS5
    Output 14 = DS6
    Output 15 = DS7
    Output 16 = DS8

---

# 4. Known FN Addresses

Six recurring address patterns have been observed:

    10000
    10001
    10010
    10011
    10100
    10110

Current interpretation:

| Address | Function | Confidence |
|---|---|---|
| `10000` | Unknown / candidate output bank | UNKNOWN |
| `10001` | Outputs 1–4 | CONFIRMED |
| `10010` | Outputs 5–8 | CONFIRMED |
| `10011` | Unknown / candidate output bank | UNKNOWN |
| `10100` | Analog companion/control | STRONG EVIDENCE |
| `10110` | Analog value | CONFIRMED / STRONG EVIDENCE |

Outputs 9–16 must not be assigned to `10000` or `10011` until captured or
hardware-traced.

---

# 5. Outputs 1–4

## Address

    10001

## Data mapping

| Data | Output | Function |
|---|---:|---|
| D1 | 1 | Alarm |
| D2 | 2 | Valve 1 |
| D3 | 3 | Valve 2 |
| D4 | 4 | Process Blower |

Example:

    Address: 10001
    Data:    0101

means:

    Output 1 OFF
    Output 2 ON
    Output 3 OFF
    Output 4 ON

---

# 6. Outputs 5–8

## Address

    10010

## Data mapping

| Data | Output | Function |
|---|---:|---|
| D1 | 5 | Regen Blower |
| D2 | 6 | Regen Heater |
| D3 | 7 | Isolation Valve |
| D4 | 8 | Process Heater |

Example:

    Address: 10010
    Data:    0011

means:

    Output 5 OFF
    Output 6 OFF
    Output 7 ON
    Output 8 ON

---

# 7. Output 6 Special Test Condition

The machine does not permit Regen Heater / Output 6 to operate without Regen
Blower / Output 5.

Therefore a clean Output-6-only machine capture cannot normally be produced.

Output 6 was isolated by comparing:

    Output 5 only

against:

    Output 5 + Output 6

The changing data field corresponded to D2 of address `10010`.

Therefore:

    10010 D2 = Output 6

This can later be independently tested by the ATOM transmitter because the
service tester will not necessarily need to enforce the machine's process
interlocks.

---

# 8. Output 7 Confirmation

Output 7 / Isolation Valve was initially inferred from the four-field
progression.

A dedicated Isolation capture subsequently changed the predicted D3 field.

Therefore:

    10010 D3 = Output 7

is confirmed.

---

# 9. Output 8 Confirmation

Output 8 was isolated by comparing:

    Output 4 only

against:

    Output 4 + Output 8

The additional change occurred in D4 of address `10010`.

Therefore:

    10010 D4 = Output 8 / Process Heater

---

# 10. Blind Validation Test

Capture:

    FN-Main U Tell Me V1PBISOGAS.csv

This capture was intentionally decoded without relying on the machine setup.

Decoded state:

| Output | State |
|---:|---|
| 1 | OFF |
| 2 | ON |
| 3 | OFF |
| 4 | ON |
| 5 | OFF |
| 6 | OFF |
| 7 | ON |
| 8 | ON |

Bitmap:

    O1 O2 O3 O4 | O5 O6 O7 O8
     0  1  0  1 |  0  0  1  1

Decoded addresses:

    10001 -> 0101
    10010 -> 0011

Human interpretation:

    Valve 1 ON
    Process Blower ON
    Isolation Valve ON
    Process Heater ON

The analog command decoded as:

    0%

The user subsequently confirmed that this was exactly how the machine had been
configured.

## Significance

This is an important independent validation because the decoder predicted the
machine state from the FN waveform rather than being told the desired state
first.

Confidence in the Outputs 1–8 mapping is therefore high.

---

# 11. 4–20 mA Analog Command

PCB-085 contains a 4–20 mA output.

Saleae captures were taken at:

    0%
    10%
    25%
    50%
    60%
    75%
    100%

Two FN addresses vary with the analog setting:

    10110
    10100

---

# 12. Primary Analog Value — Address 10110

## CONFIRMED / STRONG EVIDENCE

Address:

    10110

contains a four-bit analog value.

The transmitted data order is LSB-first:

    D1 = bit 0
    D2 = bit 1
    D3 = bit 2
    D4 = bit 3

Observed results:

| Requested | D1 D2 D3 D4 | Normal Binary D4 D3 D2 D1 | Code |
|---:|---|---|---:|
| 0% | 0000 | 0000 | 0 |
| 10% | 1000 | 0001 | 1 |
| 25% | 1100 | 0011 | 3 |
| 50% | 1110 | 0111 | 7 |
| 60% | 1001 | 1001 | 9 |
| 75% | 1101 | 1011 | 11 |
| 100% | 1111 | 1111 | 15 |

The captured values fit:

    code = floor(percent * 15 / 100)

for every percentage tested.

Therefore the analog command appears to provide:

    16 discrete levels
    code range 0–15

---

# 13. Approximate Current Interpretation

If PCB-085 maps code 0–15 linearly onto 4–20 mA:

    current_mA = 4 + code * (16 / 15)

or:

    current_mA = 4 + code * 1.0666667

Examples:

| Code | Approx. Current |
|---:|---:|
| 0 | 4.00 mA |
| 1 | 5.07 mA |
| 3 | 7.20 mA |
| 7 | 11.47 mA |
| 9 | 13.60 mA |
| 11 | 15.73 mA |
| 15 | 20.00 mA |

This current calculation is a **working interpretation**.

It should not be promoted to confirmed until actual PCB-085 output current is
measured against transmitted codes.

---

# 14. Analog Companion Address — 10100

## STRONG EVIDENCE / NOT FULLY CONFIRMED

Address:

    10100

also changes with analog command.

Observed data currently fits:

    D1 = b0
    D2 = b1
    D3 = b1
    D4 = b0

or:

    companion = [b0, b1, b1, b0]

where:

    b0 = primary analog code bit 0
    b1 = primary analog code bit 1

Examples from current captures are consistent with this relationship.

However, captured non-zero analog codes used for the initial analysis are
primarily odd values.

An even analog code is required to properly challenge this hypothesis.

### Recommended validation capture

Generate an analog percentage that produces an even code.

Examples using the current floor model:

    33% -> floor(33 * 15 / 100) = 4

or:

    40% -> floor(40 * 15 / 100) = 6

Capture one or both.

Then compare address `10100` against the predicted companion data.

Do not mark the companion rule CONFIRMED until this test is performed.

---

# 15. Outputs 9–16

## UNKNOWN

Physical outputs are known:

    O9  = DS4
    O10 = DS3
    O11 = DS2
    O12 = DS1

    O13 = DS5
    O14 = DS6
    O15 = DS7
    O16 = DS8

Two currently unexplained recurring addresses are:

    10000
    10011

These are strong candidates for the remaining output banks.

However:

**Do not assign them yet.**

Required validation:

1. command Output 9
2. capture FN traffic
3. identify changing address/data field
4. repeat for Outputs 10–12
5. repeat for Outputs 13–16

A single output from each four-output group may be sufficient to establish the
address, followed by additional captures to confirm D1-D4 ordering.

---

# 16. Raw Field Positions

Observed changes for the Outputs 5–8 bank in the raw 71-interval frame:

| Output | First Copy | Repeated Copy |
|---:|---|---|
| O5 | intervals 20–23 | 56–59 |
| O6 | intervals 24–27 | 60–63 |
| O7 | intervals 28–31 | 64–67 |
| O8 | intervals 32–34 | 68–70 |

The final field is shortened in the transition representation because of the
adjacent synchronization interval.

This clean progression strongly supports the four consecutive D1-D4 field
interpretation.

---

# 17. Known Capture Inventory

Important PCB-085 captures include:

    FN-Main No Outputs.csv

    FN-Main Alarm Output 1.csv
    FN-Main Valve 1 Output 2.csv
    FN-Main Valve 2 Output 3.csv

    FN-Main PB Output.csv
    FN-Main PB Output 4.csv

    FN-Main RB Output 5.csv
    FN-Main RB & HTR Output 5&6.csv

    FN-Main Isolation Output 7.csv

    FN-Main PB & HTR Output 4&8.csv

    FN-Main U Tell Me V1PBISOGAS.csv

Analog captures include:

    FN-Main - 0 Percent.csv
    FN-Main 25 Percent.csv
    FN-Main 50 Percent.csv
    FN-Main 75 Percent.csv
    FN-Main 100 Percent.csv

and longer captures at:

    0%
    10%
    25%
    50%
    60%
    75%
    100%

---

# 18. Mislabeled Capture Warning

An older file named:

    FN-Main RB & HTR Output 4&8.csv

was determined to be byte-for-byte identical to the legitimate:

    FN-Main RB & HTR Output 5&6.csv

It must NOT be used as evidence for Outputs 4+8.

The corrected Process Blower + Process Heater capture is:

    FN-Main PB & HTR Output 4&8.csv

Preserve this warning so future automated analysis does not accidentally use
the mislabeled capture as protocol evidence.

---

# 19. Proposed PCB-085 Software Profile

The firmware should eventually represent PCB-085 approximately as:

    PCB085
        discrete_outputs = 16
        analog_outputs = 1

        address_10001:
            D1 -> output 1
            D2 -> output 2
            D3 -> output 3
            D4 -> output 4

        address_10010:
            D1 -> output 5
            D2 -> output 6
            D3 -> output 7
            D4 -> output 8

        address_10000:
            UNKNOWN

        address_10011:
            UNKNOWN

        address_10110:
            analog_code_0_to_15
            lsb_first = true

        address_10100:
            analog_companion
            status = experimental

Do not encode these as UI-specific constants.

They belong in the PCB-085 board profile.

---

# 20. ATOM Transmitter Requirements for PCB-085

The ATOM must maintain something conceptually equivalent to:

    struct PCB085State {
        bool outputs[16];
        uint8_t analog_code;
    };

When the CYD requests:

    Output 2 ON
    Output 4 ON
    Output 7 ON
    Output 8 ON
    Analog 0%

the state becomes:

    outputs =
        0 1 0 1
        0 0 1 1
        ? ? ? ?
        ? ? ? ?

    analog_code = 0

The ATOM then continuously constructs and transmits the required FN address
cycle.

The ATOM must NOT merely transmit one packet when a CYD button is pressed.

---

# 21. Decoder Output Example

For the blind validation capture, desired decoder output is:

    PCB-085 FN FRAME

    Address 10001
    Data    0101

      O1 Alarm             OFF
      O2 Valve 1           ON
      O3 Valve 2           OFF
      O4 Process Blower    ON

    Address 10010
    Data    0011

      O5 Regen Blower      OFF
      O6 Regen Heater      OFF
      O7 Isolation Valve   ON
      O8 Process Heater    ON

    Address 10110
    Data    0000

      Analog code          0
      Requested analog     0%

The decoder should additionally expose the raw address/data rather than only
the human-readable interpretation.

---

# 22. Remaining Tests

Priority PCB-085 tests:

1. Capture Output 9.
2. Capture Output 13.
3. Determine which unknown address corresponds to each remaining output bank.
4. Confirm D1-D4 mapping for Outputs 9–16.
5. Capture an even analog code such as code 4 or code 6.
6. Validate address `10100`.
7. Measure actual 4–20 mA current versus codes 0–15.
8. Determine complete cyclic address order.
9. Determine minimum communications refresh rate before PCB-085 declares
   communications failure.
10. Generate PCB-085 traffic from ATOM and verify the real board accepts it.

---

# 23. Major Milestone Reached

The PCB-085 decoder has successfully passed a blind combined-state test.

The waveform alone correctly identified:

    Valve 1
    Process Blower
    Isolation Valve
    Process Heater
    Analog = 0%

The actual machine setup was subsequently confirmed to match.

Therefore the project has moved from basic waveform exploration into a stage
where the known PCB-085 protocol can begin being implemented algorithmically
in the ATOM/CYD tester while the remaining addresses are characterized.