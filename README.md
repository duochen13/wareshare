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
In every new terminal, activate ESP-IDF first: `get_idf` (alias for `. ~/esp/esp-idf/export.sh`).

## Verifying a milestone (manual, hardware-in-the-loop)

There is no automated test suite — each milestone is verified by flashing and observing.
The board enumerates at `/dev/cu.usbmodem101` (re-check with `ls /dev/cu.*` if missing).

**Milestone 0 — toolchain + codecs (DONE):**
```bash
# 1. Toolchain/USB/flash gate — should print "Hello world!" repeating:
cd firmware/hello_world && get_idf && idf.py -p /dev/cu.usbmodem101 flash monitor   # Ctrl-] to exit

# 2. Codec gate — flash the XiaoZhi reference and look for clean codec init:
cd ../../xiaozhi-esp32 && get_idf && idf.py -p /dev/cu.usbmodem101 flash monitor
#   PASS = log shows: ES8311: Work in Slave mode / ES7210: Work in Slave mode + Enable MIC1..MIC4
#   (the ov2640/ov5640 "camera" I2C errors are expected — no camera is populated)
```
Non-interactive capture (no monitor): `python /tmp/claude/serial_reset_capture.py` resets the
board and dumps ~18 s of boot log with a codec-init summary.

**Milestone 1a — playback:** flash `firmware/voice-assistant` and listen for a steady 440 Hz tone.
See `docs/superpowers/plans/2026-06-10-esp32-s3-voice-assistant-milestone-1a-playback.md`.
