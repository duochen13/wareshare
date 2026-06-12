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
- **Board serial port:** `/dev/cu.usbmodem101` (native USB; appeared on plug-in — no driver needed). Note: the port can disappear if the board is unplugged/reset; re-check with `ls /dev/cu.*`.
- **Flash size:** board reports **16 MB** flash (chip rev v0.2, 2 CPU cores). Set `CONFIG_ESPTOOLPY_FLASHSIZE_16MB` in our own firmware.

## ✅ Task 5 gate PASSED (2026-06-10)
- `firmware/hello_world` flashed over `/dev/cu.usbmodem101` and the serial log printed `Hello world!` + the chip banner + the restart countdown. Toolchain → USB → flashing → execution all confirmed working on IDF v5.5.

## ✅ Task 7 gate PASSED → MILESTONE 0 COMPLETE (2026-06-10)
- Built XiaoZhi from source for `CONFIG_BOARD_TYPE_WAVESHARE_ESP32_S3_AUDIO_BOARD` (the verified reference for this board), flashed it, and confirmed in the boot log:
  - `Board: SKU=esp32-s3-audio-board` (correct board auto-detected)
  - `ES8311: Work in Slave mode` (DAC up), `ES7210: Work in Slave mode` + `Enable MIC1..MIC4` + `Enable TDM mode` (4-ch ADC up)
  - `BoxAudioCodec: BoxAudioDevice initialized` → `Adev_Codec: Open codec device OK`. **No I2C errors on the codec bus, no panic.**
- **Pin map from `docs/.../reference/xiaozhi-board-reference.md` is now hardware-confirmed.**
- ⚠️ **Camera is NOT populated on our board** — the boot log's only I2C errors are `ov2640`/`ov5640` sensor-ID failures (the camera configs we copied from XiaoZhi's `config.json`). **Our own firmware must NOT enable the camera** (`CONFIG_CAMERA_*`); it's irrelevant to the audio assistant and just spams errors.
- XiaoZhi build config lives in `xiaozhi-esp32/sdkconfig.defaults` (gitignored reference clone; original saved as `sdkconfig.defaults.bak`).

> Milestone 0 done: toolchain installed (v5.5), `hello_world` flashed, audio reference boots with ES8311 + ES7210 initializing cleanly, ESP-SR/IDF compat confirmed, board audio pin map recorded **and verified on hardware**. Next: write the Milestone 1a (playback) plan using the confirmed recipe.

## ✅ Milestone 1a (playback) COMPLETE (2026-06-12)
- Our **own** firmware `firmware/voice-assistant/` plays a steady 440 Hz sine through ES8311 → NS4150B at 24 kHz — **tone confirmed audible by the user.**
- Bring-up is in `main/board_audio.c`: I²C master (sda=11/scl=10) → TCA9555 EXIO8 high (amp) → I²S TX std (mclk=12/bclk=13/ws=14/dout=16, 24 kHz, MCLK×256, 16-bit stereo) → ES8311 DAC via `esp_codec_dev` (pinned `esp_codec_dev` 1.5.10, `esp_io_expander_tca95xx_16bit` 2.0.2). `main.c` generates the sine and streams it with `esp_codec_dev_write`. **Tuned for comfortable volume: codec volume 60, amplitude 3000 (~-20 dBFS)** — initial 80/8000 was too loud on this board.
- Build/flash gotcha learned: **only one process can hold `/dev/cu.usbmodem101`** — close `idf.py monitor` before flashing or you get "No serial data received" / port-busy. Also added `esp_driver_gpio` to the component REQUIRES (board_config.h needs `driver/gpio.h`).

> Next: **Milestone 1b (capture)** — add the ES7210 RX path (duplex I²S, TDM 4-slot), record mic to a buffer, dump to the Mac, confirm intelligible. See plan §"What this sets up for Milestone 1b".

## Resolved: firmware base (was "Task 6 research")
- **Pin map confirmed** from XiaoZhi `config.h` — see `docs/superpowers/reference/xiaozhi-board-reference.md`.
- **Audio:** ES8311 (DAC, I2S std TX) + ES7210 (4-ch ADC, I2S TDM RX), duplex on `I2S_NUM_0`,
  24 kHz, MCLK ×256.
- **⚠️ Speaker amp enabled via TCA9555 IO-expander pin EXIO8** (not a GPIO). Must drive it high or
  there is no sound.

## Resolved: ESP-SR ↔ ESP-IDF compatibility (was Task 6 Step 4)
- Resolved the `espressif/esp-sr` manifest via the component registry: **every published version 2.3.1 → 2.4.6 (latest) requires only `idf >= 5.0`.**
- ⇒ Our installed **ESP-IDF v5.5 is fully compatible with ESP-SR**. No re-pin of the toolchain needed for Milestone 2 (wake word).
