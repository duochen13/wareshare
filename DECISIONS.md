# Decisions Log

| Date | Decision | Rationale |
|---|---|---|
| 2026-06-10 | Build our own firmware + Python server (not turnkey XiaoZhi) | User's goal is to learn/own the Whisper→GPT→TTS pipeline |
| 2026-06-10 | Half-duplex v1, no AEC | Removes hardest subsystem for a beginner |
| 2026-06-10 | Laptop server for v1 (not direct-to-OpenAI) | Easier iteration; API key off-device |
| 2026-06-10 | First end-to-end milestone = echo loop (no LLM) | Validate full audio round-trip before GPT |
| 2026-06-10 | Study XiaoZhi as the reference; build our own | Exact board (`esp32-s3-audio-board`) is supported by XiaoZhi → verified pin map + codec setup, no schematic archaeology |
| 2026-06-10 | Reuse `esp_codec_dev` + `esp_io_expander_tca95xx_16bit` standard components | XiaoZhi uses them too; hand-writing codec register init is pointless. We own the app logic, not the chip drivers |
| 2026-06-10 | Use ESP-IDF **v5.5** (release/v5.5), not the plan's v5.4.1 | Already installed + tools set up; v5.5 is newer and within ESP-SR's supported 5.4/5.5 line. Re-pin only if Task 6 Step 4 shows ESP-SR needs an older IDF. |

## Environment (Milestone 0)
- **ESP-IDF:** `~/esp/esp-idf` @ `release/v5.5` (`1576525dac`), tools installed (`idf5.5_py3.14_env`). `get_idf` alias added to `~/.zshrc`.
- **Host tools:** Homebrew cmake + ninja present.
- **Board serial port:** `/dev/cu.usbmodem101` (native USB; appeared on plug-in — no driver needed).

## Resolved: firmware base (was "Task 6 research")
- **Pin map confirmed** from XiaoZhi `config.h` — see `docs/superpowers/reference/xiaozhi-board-reference.md`.
- **Audio:** ES8311 (DAC, I2S std TX) + ES7210 (4-ch ADC, I2S TDM RX), duplex on `I2S_NUM_0`,
  24 kHz, MCLK ×256.
- **⚠️ Speaker amp enabled via TCA9555 IO-expander pin EXIO8** (not a GPIO). Must drive it high or
  there is no sound.

## Open (resolve before Milestone 2)
- [ ] ESP-SR version and its required ESP-IDF version (affects which ESP-IDF we pin).
