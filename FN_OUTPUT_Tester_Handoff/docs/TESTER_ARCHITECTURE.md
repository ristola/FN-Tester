# ShackMate FN Service Tester Architecture

## Target
CYD-4.3 ESP32 touchscreen service tester.

## Diagnostic Goal
Isolate whether failure is in:
- FN-MAIN/controller
- field wiring/communications
- OUTPUT-board receive circuitry
- OUTPUT-board logic/output circuitry

## Proposed Modes
### Monitor
Passive/high-impedance observation where practical.

### Main Tester
Listen to FN-MAIN and report recognizable/valid FN frames.

### Output Tester
Generate controlled FN traffic to exercise an OUTPUT board independently.

### Signal Diagnostic
Separate electrical/timing health from semantic protocol validity.

## Software Layers
```text
UI / Service Workflow
        |
Board Profile / Interpretation
        |
FN Protocol
        |
Frame / Symbol Codec
        |
Capture + Transmit Timing
        |
ESP32 Hardware Abstraction
        |
Protected / Isolated FN Interface
```

## Board Profiles
Profiles may define output count, addressing, command mapping, special commands, timing differences, and UI labels.

Initial profiles:
- PCB-110 / 10-output
- newer 16-output board — TBD

## Interface Safety
Do not connect ESP32 directly to unknown industrial wiring. Measure field-line voltage, polarity, current, transient behavior, and isolation requirements first.

## Development Order
1. Reverse-engineer conditioned receive waveform.
2. Establish symbol/frame format.
3. Build/test Saleae decoder.
4. Correlate commands with PCB-110 outputs.
5. Determine transmit waveform.
6. Build protected ESP32 receive interface.
7. Implement passive CYD monitor.
8. Add controlled transmitter.
9. Add OUTPUT-board tests.
10. Add additional board profiles.
