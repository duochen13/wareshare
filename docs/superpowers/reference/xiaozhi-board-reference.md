# Board Reference — derived from studying XiaoZhi (`esp32-s3-audio-board`)

**Purpose:** We build our own firmware, but we study XiaoZhi's verified implementation for the
exact board so our hardware bring-up is correct from day one. This file is the distilled,
trustworthy reference. Source files studied (in `xiaozhi-esp32/`):

- `main/boards/waveshare/esp32-s3-audio-board/config.h` — pin map
- `main/boards/waveshare/esp32-s3-audio-board/esp32-s3-audio_board.cc` — board init (I2C, IO expander, codec wiring)
- `main/audio/codecs/box_audio_codec.cc` — the ES8311 + ES7210 audio codec setup

---

## 1. Verified pin map (ESP32-S3-AUDIO-Board)

| Function | GPIO |
|---|---|
| I2C SDA (codec + IO expander control) | 11 |
| I2C SCL | 10 |
| I2S MCLK | 12 |
| I2S BCLK | 13 |
| I2S WS (LRCLK) | 14 |
| I2S DIN (mic data in, from ES7210) | 15 |
| I2S DOUT (speaker data out, to ES8311) | 16 |
| BOOT button | 0 |
| RGB LED strip (WS2812, 6 LEDs) | 38 |

- Audio sample rate: **24 kHz** input AND output. MCLK = 256 × sample rate.
- I2C IO expander: **TCA9555** (16-bit), I2C address `...000` (0x20).

---

## 2. ⚠️ THE critical gotcha: the speaker amp is behind the IO expander

The speaker amplifier enable is **NOT a direct GPIO**. It is driven through the **TCA9555 I²C
IO-expander, pin EXIO8**. In XiaoZhi's code (`esp32-s3-audio_board.cc:63`):

```cpp
esp_io_expander_set_level(io_expander, IO_EXPANDER_PIN_NUM_8, 1); // 启用喇叭功放 = enable speaker amp
```

That is why `AUDIO_CODEC_PA_PIN` is `GPIO_NUM_NC` in the config — there is no PA GPIO for the
codec driver to toggle. **If our firmware does not initialize the TCA9555 and set EXIO8 high, the
ES8311 will receive data but the speaker will be silent.** This is the #1 thing to get right in
the playback milestone.

(EXIO0/EXIO1 reset the LCD/touch; EXIO5/EXIO6 reset/enable the camera. For audio-only we only
need EXIO8.)

---

## 3. Audio architecture (what we reuse vs. what we build)

**Reuse (standard Espressif components — not "XiaoZhi code"):**
- `espressif/esp_codec_dev` — provides `es8311_codec_new` (DAC) and `es7210_codec_new` (4-ch ADC)
  plus the `esp_codec_dev_read/write/open/close` API. This is the standard way; XiaoZhi just wraps it.
- `espressif/esp_io_expander_tca95xx_16bit` — the TCA9555 driver for the amp-enable pin.

**Build ourselves (the actual learning goal):** the audio capture loop, wake-word integration,
the WebSocket streaming protocol, the Python server, and the assistant logic.

### I2S configuration (from `box_audio_codec.cc`)
- One I2S peripheral, `I2S_NUM_0`, in **duplex** mode: `i2s_new_channel()` returns both a TX and
  an RX handle that share the same BCLK/WS/MCLK pins.
- **TX (to ES8311 / speaker):** `i2s_std` mode, stereo slots, 16-bit, MCLK ×256, master.
  `dout = GPIO16`, `din = unused`.
- **RX (from ES7210 / mics):** `i2s_tdm` mode with **4 slots** (the ES7210 is a 4-channel ADC),
  16-bit, `bclk_div = 8`. `din = GPIO15`, `dout = unused`.

### Stream formats
- **Output (playback):** mono, 16-bit, 24 kHz. `esp_codec_dev_write(output_dev, data, bytes)`.
- **Input (capture):** opened as **4 channels**, 16-bit, 24 kHz. Useful data is **channel 0**
  (the processed mic) and, when echo-reference is on, **channel 1** (the reference). Input gain
  ~30 dB applied to channel 0. `esp_codec_dev_read(input_dev, dest, bytes)`.
- `AUDIO_INPUT_REFERENCE = true`: channel 1 carries the loopback reference used for AEC. **For our
  half-duplex v1 we don't need AEC**, so we can capture channel 0 only and ignore the reference —
  but the ADC still streams 4 channels, so we must read 4-ch frames and extract channel 0.

---

## 4. Concrete recipe for our playback + capture milestone

1. `idf.py add-dependency "espressif/esp_codec_dev"` and `"espressif/esp_io_expander_tca95xx_16bit"`.
2. Init I2C master bus: port 0, `sda=11`, `scl=10`, default clock.
3. Init TCA9555 at 0x20; set EXIO8 direction = output; **set EXIO8 = 1 (enable amp).**
4. Create duplex I2S on `I2S_NUM_0` with the pin map above (TX std, RX TDM 4-slot, 24 kHz, MCLK ×256).
5. Build the ES8311 output codec dev (DAC mode, `use_mclk = true`) and the ES7210 input codec dev
   (`mic_selected = MIC1|MIC2|MIC3|MIC4`).
6. **Playback test:** open output mono/16-bit/24 kHz, write a generated tone or a bundled WAV → hear it.
7. **Capture test:** open input 4-ch/16-bit/24 kHz, read frames, keep channel 0, stream to the Mac → verify intelligible.

This recipe is the basis for the (to-be-written) Milestone 1 plan, with full code.

---

## 5. Still to study later (when those milestones arrive)
- **Wake word / AFE:** how XiaoZhi feeds the ES7210 stream into ESP-SR (AFE: NR + VAD) and WakeNet
  — see `main/audio/` processing and the `esp-sr` managed component.
- **Protocol/server:** XiaoZhi's WebSocket/MQTT protocol (we will design our own simpler one, but
  its framing and audio-chunking choices are worth a look).
