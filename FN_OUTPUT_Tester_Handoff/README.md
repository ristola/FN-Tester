# ShackMate FN OUTPUT Tester

Project handoff/context package for the CYD-4.3 FN service tester and Saleae FN decoder.

## Setup
1. Copy this folder into your VS Code project/repository.
2. Put the original Saleae export at `captures/digital.csv`.
3. Open the repository in VS Code.
4. Ask Claude to read `CLAUDE.md` and all files in `docs/` before analyzing or changing protocol code.
5. Preserve original captures.

## Suggested First Claude Prompt
Read `CLAUDE.md` and every Markdown file under `docs/`. Then inspect `captures/digital.csv`. Do not assume the FN protocol format. First characterize raw timing, polarity, pulse-width populations, gaps, repetitions, and candidate frame boundaries. Update `docs/FN_PROTOCOL_FINDINGS.md` with evidence and confidence levels before implementing semantic decoding.
