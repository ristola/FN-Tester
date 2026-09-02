# RTS FN OUTPUT Tester — AI Project Context

## Purpose

This repository is for reverse-engineering and building a field/service tester
for UNA-DYN/FN equipment communications.

Primary architecture:

- **M5Stack ATOM pod** = physical FN communications interface
- **CYD-4.3 ESP32 touchscreen** = untethered user interface / service console
- **ESP-NOW** = ATOM ↔ CYD communications

The tester must independently diagnose both sides of the FN system:

1. FN-MAIN communications output
2. FN OUTPUT-board receive/control operation

The design must support multiple generations of FN OUTPUT boards and
firmware/protocol behavior.

---

# Authoritative Project Documents

Before modifying protocol-related code, read:

- `docs/FN_PROTOCOL_FINDINGS.md`
- `docs/PCB085_ANALYSIS.md`
- `docs/PCB110_ANALYSIS.md`
- `docs/SALEAE_DECODER_NOTES.md`
- `docs/TESTER_ARCHITECTURE.md`
- `docs/EXPERIMENT_LOG.md`

Use this file (`CLAUDE.md`) as the project operating context.

Use the documents under `docs/` as the detailed engineering evidence and
protocol record.

If this file conflicts with a newer documented experiment or capture analysis,
flag the contradiction before changing code.

---

# Current Focus

Current development focus is:

- FN 2-wire communications
- PCB-085 16-output + 4–20 mA protocol
- Saleae decoder improvements
- ATOM FN receiver/transmitter implementation
- reusable FN frame encoder/decoder
- PCB-085 board profile
- CYD control/diagnostic UI

Fiber-optic communications are part of the FN system but are not the initial
physical-interface implementation target.

The ATOM architecture must allow a fiber interface to be added later without
rewriting the FN protocol layer.

---

# Evidence Rules

Use these categories:

## CONFIRMED

Directly:

- traced
- measured
- read from a reliable datasheet
- repeatedly demonstrated by controlled captures/tests
- independently validated

## STRONG EVIDENCE

Strongly supported by multiple observations but not fully verified.

## HYPOTHESIS

Plausible interpretation requiring additional testing.

## UNKNOWN

Not yet determined.

Do not silently promote a hypothesis to a fact.

For protocol conclusions preserve:

- capture filename
- experiment/setup
- raw timing or symbol evidence
- decoded interpretation
- confidence level
- contradictions or alternate interpretations

Raw Saleae captures are evidence.

**Never modify raw captures in place.**

---

# Software Design Rule

Keep these layers separate:

1. physical interface / protection / isolation
2. RMT / GPIO capture and transmit
3. interval/timing classification
4. FN symbol codec
5. FN address + D1-D4 codec
6. board profile
7. state manager
8. ESP-NOW transport
9. CYD UI / service workflow

Do not bury board-specific protocol assumptions inside UI code.

The common FN protocol layer should answer:

    What address and D1-D4 data were transmitted?

The board profile should answer:

    What does that address/data mean for PCB-085, PCB-110, etc.?

---

# PCB-085 — Current Verified Snapshot

Detailed PCB-085 evidence belongs in:

    docs/PCB085_ANALYSIS.md

This section is only the implementation-level summary.

## Hardware

### CONFIRMED

PCB-085 contains:

- 16 discrete outputs
- one 4–20 mA analog output
- FN communications through fiber-optic / 2-wire communications
- no separate external analog-command input
- five visible MC145027 decoder ICs

Loss of valid communications causes commanded PCB-085 outputs to turn OFF.

The analog section includes a confirmed AD694 4–20 mA transmitter plus
additional analog conditioning, decoding, scaling, and calibration circuitry.

Do not treat other analog IC part numbers as confirmed unless their markings
have been positively verified.

---

# PCB-085 Physical Output Mapping

## CONFIRMED

Output numbering runs right-to-left on the PCB.

| Output | LED |
|---:|---|
| 1 | DS12 |
| 2 | DS11 |
| 3 | DS10 |
| 4 | DS9 |
| 5 | DS13 |
| 6 | DS14 |
| 7 | DS15 |
| 8 | DS16 |
| 9 | DS4 |
| 10 | DS3 |
| 11 | DS2 |
| 12 | DS1 |
| 13 | DS5 |
| 14 | DS6 |
| 15 | DS7 |
| 16 | DS8 |

Physical grouping is therefore:

- Outputs 1–4
- Outputs 5–8
- Outputs 9–12
- Outputs 13–16

This is consistent with the observed four-data-field FN structure.

---

# PCB-085 Known Output Functions

## CONFIRMED / validated

| Output | Function |
|---:|---|
| 1 | Alarm |
| 2 | Valve 1 |
| 3 | Valve 2 |
| 4 | Process Blower |
| 5 | Regen Blower |
| 6 | Regen Heater |
| 7 | Isolation Valve |
| 8 | Process Heater |

Outputs 9–16 are not yet function-mapped.

Output 6 cannot normally be commanded alone by the machine because the machine
also commands Regen Blower / Output 5.

Output 6 was isolated by comparing:

- Output 5 only
- Outputs 5 + 6

Output 7 was independently captured and confirmed.

Output 8 was isolated by comparing:

- Output 4 only
- Outputs 4 + 8

---

# FN Transmission Model

Detailed common timing information belongs in:

    docs/FN_PROTOCOL_FINDINGS.md

## Current working model

A decoded FN word behaves as:

    A1 A2 A3 A4 A5 | D1 D2 D3 D4

where:

- first 5 fields behave as address/select
- final 4 fields behave as data
- the encoded word is transmitted twice around a sync interval

Conceptually:

    ADDRESS | DATA
       SYNC
    ADDRESS | DATA

Observed interval classes include approximately:

    SHORT = 25–27 µs
    LONG  = 180–182 µs

Observed sync-like intervals include approximately:

    1.420 ms
    1.265 ms

Do not require one exact sync duration globally.

Timing thresholds and tolerances should be configurable and preferably
profile-aware.

---

# Continuous State Requirement

## CONFIRMED

FN OUTPUT control is continuously refreshed state.

Do not implement output operations as one-shot messages.

The ATOM must maintain the complete requested board state and continuously
transmit the appropriate FN cycle while the command remains active.

Conceptually:

    requested_state.output[4] = ON

means future FN cycles continue to contain Output 4 ON.

It does NOT mean:

    send one Output-4 command and stop

Simultaneous outputs must be supported.

---

# PCB-085 Known Address Mapping

Recurring PCB-085 address patterns currently observed include:

    10000
    10001
    10010
    10011
    10100
    10110

## CONFIRMED

### Address `10001`

Outputs 1–4:

    D1 = Output 1 / Alarm
    D2 = Output 2 / Valve 1
    D3 = Output 3 / Valve 2
    D4 = Output 4 / Process Blower

### Address `10010`

Outputs 5–8:

    D1 = Output 5 / Regen Blower
    D2 = Output 6 / Regen Heater
    D3 = Output 7 / Isolation Valve
    D4 = Output 8 / Process Heater

---

# Blind Validation Test

A blind Saleae capture was decoded before the actual commanded machine state
was revealed.

Decoded:

    O1 OFF
    O2 ON
    O3 OFF
    O4 ON

    O5 OFF
    O6 OFF
    O7 ON
    O8 ON

    Analog = 0%

Bitmap:

    O1 O2 O3 O4 | O5 O6 O7 O8
     0  1  0  1 |  0  0  1  1

Human interpretation:

- Valve 1 ON
- Process Blower ON
- Isolation Valve ON
- Process Heater ON
- analog command 0%

The actual machine setup was subsequently confirmed to match exactly.

This is an important independent validation of the current PCB-085 decoder
model.

---

# PCB-085 4–20 mA Analog Command

Detailed evidence belongs in:

    docs/PCB085_ANALYSIS.md

Long Saleae captures exist at:

- 0%
- 10%
- 25%
- 50%
- 60%
- 75%
- 100%

## Primary analog value

Address:

    10110

carries a 4-bit analog value.

The data is transmitted LSB-first:

    D1 = bit 0
    D2 = bit 1
    D3 = bit 2
    D4 = bit 3

Observed:

| Requested | D1 D2 D3 D4 | Code |
|---:|---|---:|
| 0% | 0000 | 0 |
| 10% | 1000 | 1 |
| 25% | 1100 | 3 |
| 50% | 1110 | 7 |
| 60% | 1001 | 9 |
| 75% | 1101 | 11 |
| 100% | 1111 | 15 |

Current working conversion:

    analog_code = floor(percent * 15 / 100)

This relationship matches all captured test points.

Do not assume untested percentage points are confirmed.

Do not assume exact output current solely from the code until actual PCB-085
4–20 mA current measurements are performed.

---

# PCB-085 Analog Companion Address

Address:

    10100

also changes with the analog command.

Current working relationship:

    D1 = b0
    D2 = b1
    D3 = b1
    D4 = b0

where:

    b0 = analog code bit 0
    b1 = analog code bit 1

Status:

    STRONG EVIDENCE / NOT FULLY CONFIRMED

An even analog code capture is required to properly validate this relationship.

Do not hard-code this rule as confirmed protocol behavior without clearly
marking it experimental.

---

# PCB-085 Remaining Unknown Addresses

Currently unexplained:

    10000
    10011

These may correspond to:

- Outputs 9–12
- Outputs 13–16
- another PCB-085 function

Do not assign them until supported by a capture or hardware trace.

Preferred next tests include:

- Output 9
- Output 13

These should quickly identify the remaining output-bank addresses if that
hypothesis is correct.

---

# PCB-110 Status

PCB-110 remains a separate board profile.

Detailed evidence belongs in:

    docs/PCB110_ANALYSIS.md

## CONFIRMED / directly observed

- UNA-DYN PCB-110
- approximately 1996 generation
- 10 solid-state outputs
- two MC145027P decoder ICs
- MC145027 U1 pin 9 = DIN
- U1 pin 9 connects to the center pin of the FO/Wire selector
- selector determines which receive path feeds decoder DIN

## STRONG EVIDENCE

- U7 appears to be a 74HC14 Schmitt-trigger inverter
- selector position 1 appears connected to fiber receiver path
- selector position 3 connects to U7 pin 2
- U7 pin 2 appears to be conditioned wired communications
- wired receive likely passes through input conditioning and U7 before decoder DIN

Do not merge PCB-110 and PCB-085 address/output mappings.

Use separate profiles.

---

# Saleae Decoder

The Saleae decoder should progressively produce:

1. edge timing
2. short/long interval classification
3. sync detection
4. symbol boundaries
5. repeated-word detection
6. repeated-word comparison
7. 5-field address
8. D1-D4 data
9. board-profile interpretation
10. named outputs when known
11. analog value when supported
12. timing/errors/confidence

The decoder must preserve raw FN information.

Preferred output:

    PCB-085

    Address: 10001
    Data:    0101

    O1 Alarm             OFF
    O2 Valve 1           ON
    O3 Valve 2           OFF
    O4 Process Blower    ON

and:

    Address:     10110
    Data:        1001
    Analog code: 9
    Requested:   ~60%

Never hide the raw address and D1-D4 fields behind only human-readable names.

---

# ATOM Responsibilities

The M5Stack ATOM is the time-critical FN interface pod.

ATOM should handle:

- protected / isolated 2-wire electrical interface
- future fiber interface
- GPIO/RMT capture
- pulse timing
- FN symbol decoding
- FN address/data decoding
- FN frame generation
- deterministic FN transmission
- continuous cyclic state refresh
- board-profile interpretation
- communications health/watchdog information
- ESP-NOW communication with CYD

Use ESP32 RMT or equivalent deterministic hardware timing where practical.

Do not generate time-critical FN waveforms from ordinary CYD UI code.

---

# CYD Responsibilities

The CYD-4.3 is the untethered touchscreen/service console.

CYD should handle:

- user interface
- board/profile selection
- individual output control
- simultaneous output control
- analog command entry
- decoded status
- communications diagnostics
- ATOM pairing
- service workflows
- test sequencing
- ESP-NOW high-level messaging

CYD should issue logical commands such as:

    Set PCB-085 Output 4 ON
    Set PCB-085 Output 8 ON
    Set PCB-085 analog to 60%
    All Outputs OFF

The CYD should not need knowledge of individual S/L pulse timings.

---

# OUTPUT-BOARD TESTER Mode

When testing an OUTPUT board:

1. establish valid FN communications
2. begin with all commanded outputs OFF
3. continuously transmit the required FN cycle
4. maintain the entire requested output bitmap
5. allow simultaneous outputs
6. support individual output control
7. support All Off
8. eventually support Walk Test
9. support analog control for profiles that provide it
10. expose communications state/status to the operator

Never implement an output command as one transmission followed by silence.

---

# FN-MAIN ANALYZER Mode

ATOM connects to the FN-MAIN communications output and acts as the receiver.

Technician operates the machine diagnostic controls.

System should display:

- detected FN address
- D1-D4
- board-profile interpretation
- output states
- analog command
- timing
- malformed frame information
- repeated-frame mismatch
- unknown addresses
- confidence/status

Purpose:

Verify FN-MAIN communications independently of the OUTPUT board.

---

# CAPTURE / LEARN Mode

Support guided development of unknown board profiles.

Store:

- raw timing
- raw S/L symbols
- addresses
- D1-D4 fields
- known output correlations
- analog correlations
- source capture filenames
- experiment setup
- confidence

Do not attempt to learn every possible combination.

Capture individual outputs and enough simultaneous combinations to verify that
state fields combine independently.

---

# Board Profiles

Known profiles currently include:

- PCB-085
- PCB-110

Do not use one global FN output map.

Each board profile should own:

- board identifier
- number of discrete outputs
- output names
- address-to-output mapping
- analog capabilities
- analog encoding
- known timing differences
- address cycle information
- watchdog/refresh requirements
- confidence metadata

Conceptually:

    profiles/
        pcb085
        pcb110

---

# Preferred Software Structure

Prefer separation conceptually similar to:

    protocol/
        fn_timing
        fn_symbol_codec
        fn_frame_codec

    profiles/
        pcb085
        pcb110

    hardware/
        fn_rx
        fn_tx
        rmt
        interface

    state/
        output_state
        analog_state

    transport/
        espnow

    ui/
        output_tester
        fn_analyzer
        diagnostics

Exact repository structure may differ.

Preserve separation of concerns rather than forcing unnecessary directory
changes.

---

# Current Implementation Priority

The project is no longer at the basic "find the pulse timing" stage.

Current priority:

1. inspect the existing ATOM/CYD firmware
2. inspect the existing Saleae decoder
3. compare current implementation against documented PCB-085 findings
4. identify obsolete assumptions
5. implement reusable FN address + D1-D4 encode/decode
6. create or update the PCB-085 profile
7. implement verified Outputs 1–8
8. leave Outputs 9–16 unknown/placeholders until captured
9. implement address `10110` analog code 0–15
10. keep address `10100` logic explicitly experimental
11. maintain complete output state
12. implement continuous cyclic transmission
13. preserve PCB-110 support
14. preserve raw Saleae decoder visibility
15. avoid unnecessary rewrites of working ESP-NOW/UI code

---

# Important Capture Warning

An older capture named:

    FN-Main RB & HTR Output 4&8.csv

was determined to be byte-for-byte identical to the legitimate:

    FN-Main RB & HTR Output 5&6.csv

Do NOT use the older mislabeled file as evidence for Outputs 4+8.

The corrected Process Blower + Process Heater capture is:

    FN-Main PB & HTR Output 4&8.csv

See `docs/PCB085_ANALYSIS.md` for the capture/evidence record.

---

# Instructions to Claude / Other AI Assistants

Before changing protocol logic:

1. Read this file completely.
2. Read `docs/FN_PROTOCOL_FINDINGS.md`.
3. Read the relevant board analysis document.
4. Inspect existing source before proposing changes.
5. Separate measurements from interpretations.
6. Preserve CONFIRMED / STRONG EVIDENCE / HYPOTHESIS / UNKNOWN distinctions.
7. Cite the capture or experiment supporting new protocol conclusions.
8. Update documentation when conclusions change.
9. Never overwrite raw captures.
10. Preserve multi-board support.
11. Flag contradictions rather than silently choosing one answer.
12. Do not substitute generic MC145026/MC145027 assumptions for captured FN behavior.
13. Keep board interpretation separate from common FN decoding.
14. Keep protocol logic separate from UI.
15. Prefer algorithmic FN frame generation over canned waveform replay.
16. Treat continuous state refresh as fundamental.
17. Do not assign Outputs 9–16 until verified.
18. Do not treat the `10100` analog companion relationship as fully confirmed yet.
19. Do not silently change established pin/address/output mappings.
20. Avoid large architectural rewrites unless they are necessary.

---

# Beginning a Coding Session

Before editing source, first report:

1. what you understand about PCB-085
2. what you understand about PCB-110
3. what you understand about the common FN frame format
4. verified PCB-085 address mappings
5. current analog decoding status
6. ATOM responsibilities
7. CYD responsibilities
8. current repository architecture
9. files/modules that need modification
10. assumptions in the current code that conflict with current evidence
11. proposed implementation plan

Then wait for approval before making broad protocol or architecture changes.

Small, clearly requested edits may be made directly.

---

# Engineering Principle

Do not build the tester around assumptions about what FN communications
"should" look like.

Build it around what the actual hardware and captures demonstrate.

Preserve enough raw information that future discoveries can correct a board
profile without requiring the low-level capture/transmit architecture to be
rewritten.