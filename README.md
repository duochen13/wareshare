# ESP32-S3 Voice Assistant

Wake-word voice assistant on the Waveshare ESP32-S3-AUDIO-Board.
Pipeline: wake word → record → Whisper → (GPT) → TTS → speaker.

Built from scratch (our own ESP-IDF firmware + a Python server), using the XiaoZhi
firmware purely as a verified reference for this exact board.

## Docs
- Design spec: `docs/superpowers/specs/2026-06-10-esp32-s3-voice-assistant-design.md`
- Plans: `docs/superpowers/plans/`
- Board reference (from studying XiaoZhi): `docs/superpowers/reference/xiaozhi-board-reference.md`
- Decisions log: `DECISIONS.md`

## Layout
- `firmware/` — ESP32-S3 firmware (ESP-IDF, C)
- `server/`  — Python orchestration server (runs on a Mac)
- `xiaozhi-esp32/` — reference clone only; NOT part of our build (git-ignored)

## Setup
See the Milestone 0 plan for toolchain install + first flash.
