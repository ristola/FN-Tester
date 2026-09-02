# FN Communications Protocol Findings

## Purpose

This document records experimentally observed properties of the UNA-DYN/FN
communications protocol.

It is intended to describe the common FN transport/protocol independently of
any specific OUTPUT-board generation.

Board-specific mappings belong in documents such as:

- `PCB085_ANALYSIS.md`
- `PCB110_ANALYSIS.md`

Raw Saleae captures are evidence and must never be modified in place.

---

# Confidence Definitions

Protocol findings must use one of these confidence levels:

## CONFIRMED
Directly measured, repeatedly demonstrated, hardware-traced, or independently
validated.

## STRONG EVIDENCE
Supported by multiple observations but still missing a definitive validation.

## HYPOTHESIS
A plausible explanation requiring additional testing.

## UNKNOWN
Not enough evidence currently exists.

Do not silently promote a hypothesis to a confirmed protocol property.

---

# 1. Physical Transport

## CONFIRMED

FN equipment can communicate with OUTPUT boards using:

- 2-wire electrical communications
- fiber-optic communications

OUTPUT boards may contain a selector allowing the appropriate physical receive
path to feed the decoder circuitry.

The protocol carried by the physical interface is pulse/timing encoded.

Valid communications must continue to be received for the OUTPUT board to
maintain commanded states.

Loss of valid communications causes the OUTPUT board to enter its
communications-failure behavior.

The exact failure behavior may depend on board generation and must therefore be
handled by the board profile rather than assumed globally.

---

# 2. Decoder Family

## CONFIRMED

Multiple FN OUTPUT boards use Motorola MC145027-family serial decoder ICs.

The MC145027 architecture is an important clue to the FN encoding.

However:

**Do not derive FN protocol behavior solely from the MC145026/MC145027
datasheet.**

Actual FN behavior must be determined from:

- Saleae captures
- hardware tracing
- controlled output tests
- comparisons between board generations

---

# 3. Observed Timing

## CONFIRMED / STRONG EVIDENCE

Saleae captures show two primary interval classes.

### Short interval

Approximately:

    25–27 µs

### Long interval

Approximately:

    180–182 µs

A practical decoder threshold can therefore initially classify:

    interval < 100 µs  -> SHORT
    interval > 100 µs  -> LONG

The threshold should remain configurable rather than being permanently
hard-coded.

---

# 4. Synchronization Interval

Two sync-like timing values have been observed in captures:

    approximately 1.4195–1.4198 ms

and:

    approximately 1.2645–1.2647 ms

These differences may reflect:

- board generation
- FN-MAIN firmware generation
- oscillator/component tolerance
- protocol variation

Do not assume that every FN system uses exactly one sync duration.

Decoder implementations should recognize a valid sync range rather than a
single exact value.

---

# 5. Frame / Word Structure

## STRONG EVIDENCE

A normal captured FN transmission word contains approximately:

    5 address/select fields
    4 data fields

Conceptually:

    A1 A2 A3 A4 A5 | D1 D2 D3 D4

The word is followed by a synchronization interval and then repeated.

Conceptual transmission:

    ADDRESS | DATA
       sync
    ADDRESS | DATA

The repeated word provides a useful validation mechanism.

A decoder should compare both copies and flag disagreement.

---

# 6. Raw Transition Representation

Saleae CSV files contain transitions rather than already-decoded pulses.

Typical CSV columns:

    Time [s]
    Channel 0

For timestamps:

    t0, t1, t2, ...

calculate:

    dt[n] = t[n+1] - t[n]

The resulting `dt` sequence contains the short, long, and synchronization
intervals.

A typical complete frame burst contains approximately:

    72 transitions
    71 intervals

The observed structure can be represented as:

    35 intervals
    sync
    35 intervals

The two 35-interval portions normally represent the same encoded word.

---

# 7. Short / Long Symbol Representation

For initial decoding, classify each interval as:

    S = short
    L = long

Observed data-state patterns include:

    SLSL
    LSLS

These two patterns represent the two primary binary-like data states observed
in the FN captures.

Working abstraction:

    SLSL = 0
    LSLS = 1

For the final field immediately adjacent to sync, the transition representation
may appear truncated:

    SLS
    LSL

because the synchronization interval replaces or obscures the normal final
transition interval.

The decoder must account for this rather than treating the final field as
invalid.

---

# 8. Address + Data Abstraction

The most useful current high-level representation is:

    XXXXX YYYY

where:

    XXXXX = 5-bit address/select
    YYYY  = four data states D1-D4

Example:

    10001 0101

This representation should be preserved in diagnostic output even when a
board-specific profile translates it into named outputs.

Example:

    Raw FN:
        Address = 10001
        Data    = 0101

    PCB-085 interpretation:
        Output 1 OFF
        Output 2 ON
        Output 3 OFF
        Output 4 ON

---

# 9. Continuous State Transmission

## CONFIRMED

FN OUTPUT control behaves as continuously refreshed state rather than
momentary commands.

The FN-MAIN repeatedly sends the desired output state.

Therefore an FN transmitter/emulator must:

1. maintain complete desired state
2. encode the state into the appropriate addressed words
3. continuously cycle those words
4. continue transmitting while the state is required

A command such as:

    Output 4 ON

must NOT mean:

    transmit one Output-4 packet and stop

Instead it means:

    requested_state.output4 = ON

and subsequent cyclic FN transmissions must continue to contain that state.

---

# 10. Simultaneous Outputs

## CONFIRMED

Multiple outputs can be commanded simultaneously.

Captured tests demonstrate independent composition of multiple D1-D4 fields.

Therefore output states should be represented internally as bitmaps or
equivalent state structures.

Example:

    O1 O2 O3 O4
     0  1  0  1

represents two simultaneous active outputs.

---

# 11. Recurring Addresses Observed

Current PCB-085 captures repeatedly contain:

    10000
    10001
    10010
    10011
    10100
    10110

The meanings of these addresses are board-specific and therefore documented in:

    PCB085_ANALYSIS.md

Do not assume these addresses have the same meanings on PCB-110 or other FN
boards.

---

# 12. Approximate Transmission Cadence

Observed FN traffic is continuous.

Individual addressed words recur cyclically.

Earlier captures show a strong approximately 50 ms communications rhythm in
some systems.

Long PCB-085 captures show each major address recurring repeatedly throughout
the capture.

Exact scheduling/cycle behavior should continue to be measured before the
transmitter timing is considered fully reproduced.

The ATOM transmitter should eventually reproduce:

- symbol timing
- synchronization timing
- word repetition
- address cycle
- refresh/watchdog timing

---

# 13. Decoder Pipeline

Recommended decoder architecture:

    GPIO / Saleae transitions
             |
             v
       interval timing
             |
             v
       S / L classifier
             |
             v
       sync detector
             |
             v
       symbol decoder
             |
             v
       repeated-word validation
             |
             v
       address + D1-D4
             |
             v
       board profile
             |
             v
       named outputs / analog

Keep these stages separate.

---

# 14. Error Detection

The decoder should eventually detect:

- invalid interval
- missing sync
- malformed symbol
- first/second word mismatch
- unknown address
- unsupported board mapping
- timing outside tolerance
- incomplete frame

Diagnostic output should retain the raw timing/frame whenever possible.

---

# 15. ATOM Implementation Requirements

The ATOM pod should perform time-critical FN processing.

Preferred ESP32 peripheral:

    RMT

or another hardware timing mechanism capable of deterministic pulse
capture/transmission.

Do not generate critical FN timing from CYD UI code.

ATOM responsibilities include:

- RX edge capture
- interval classification
- FN frame decoding
- FN frame generation
- continuous cyclic transmission
- board-profile translation
- communications watchdog/status
- ESP-NOW communication with CYD

---

# 16. CYD Implementation Requirements

CYD operates at the logical/service level.

Examples:

    SET OUTPUT 4 ON
    SET OUTPUT 8 ON
    SET ANALOG 60%
    ALL OUTPUTS OFF
    START PASSIVE MONITOR

CYD should not need to know individual RMT timing values.

---

# 17. Unknown / Future Work

Still to determine:

- exact tolerance ranges for S/L timing
- exact allowed sync range by board generation
- complete address cycle ordering
- exact refresh/watchdog timing requirements
- whether address interpretation differs between generations
- polarity differences between physical interfaces
- whether all FN generations use identical symbol encoding
- error/retry semantics, if any
- behavior of additional board generations

Do not hard-code unresolved assumptions as universal FN protocol rules.

---

# Engineering Rule

The common FN decoder should answer:

    "What FN address and data were transmitted?"

A board profile should answer:

    "What does that address/data mean on this particular board?"

Keep those two questions separate in the software architecture.