# ESP32-S3 Voice Assistant — Implementation Plan: Milestone 1b (Mic Capture)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.
>
> **NOTE — hardware in the loop:** the final verification (Task 3) needs the board on `/dev/cu.usbmodem101`, a human to **speak** during the record window, and to **listen** to the resulting WAV on the Mac. Build steps are automatable.
>
> **⚠️ Serial-port contention (cost time last session):** only ONE process can hold `/dev/cu.usbmodem101`. Close any `idf.py monitor` before flashing or running the capture script, or you'll get "No serial data received"/port-busy.

**Goal:** Extend our firmware to record the ES7210 microphone and stream ~3 s of audio over USB-serial to the Mac, which saves it as a WAV — done when you play the WAV and your speech is intelligible.

**Architecture:** Builds on Milestone 1a's `board_audio` module. The I²S channel becomes **duplex** (TX for ES8311 + RX for ES7210), sharing one `esp_codec_dev` I²S data interface. We add the ES7210 input device (4-channel TDM ADC), open it at 24 kHz/16-bit/4-ch with 30 dB gain on channel 0, and a `board_audio_read_mono()` helper that reads 4-ch frames and keeps channel 0. `app_main` records 3 s into base64 lines printed between markers; a Mac-side Python tool (`tools/capture_mic.py`) resets the board, reads the serial stream, decodes it, and writes a WAV. (Wi-Fi streaming is Milestone 3; serial dump is the right tool for 1b.)

**Tech Stack:** ESP-IDF v5.5, C; `esp_codec_dev` 1.5.10 (ES7210 driver), mbedtls base64 (bundled). Mac side: Python 3 `pyserial` + stdlib `wave`. Board: ES7210 is a **4-channel** ADC; useful audio is channel 0 (see `docs/superpowers/reference/xiaozhi-board-reference.md` §3).

**Source of truth:** the verified XiaoZhi init in `xiaozhi-esp32/main/audio/codecs/box_audio_codec.cc` (duplex channels lines 94–182; ES7210 input lines 62–75; input open/gain lines 195–206).

---

## File Structure

```
firmware/voice-assistant/main/
├── board_config.h     # MODIFY: add mic channel count + gain constants
├── board_audio.h      # MODIFY: add get_input_dev() + read_mono()
├── board_audio.c      # MODIFY: duplex I2S, shared data_if, ES7210 input, read_mono
├── CMakeLists.txt      # MODIFY: add mbedtls to REQUIRES (base64)
└── main.c             # REWRITE: record 3 s + base64 dump (no tone)
tools/
└── capture_mic.py     # CREATE: Mac-side reset → read serial → write WAV
```

---

## Task 1: Make I²S duplex and add the ES7210 input codec

**Goal:** bring up the mic path alongside the existing speaker path. No capture yet — prove ES7210 opens cleanly.

**Files:**
- Modify: `firmware/voice-assistant/main/board_config.h`
- Modify: `firmware/voice-assistant/main/board_audio.h`
- Modify: `firmware/voice-assistant/main/board_audio.c`
- Modify: `firmware/voice-assistant/main/main.c`

- [ ] **Step 1: Add mic constants to `board_config.h`**

Append to `firmware/voice-assistant/main/board_config.h` (before the final newline):
```c

/* ES7210 is a 4-channel ADC; we keep channel 0. */
#define BOARD_MIC_CHANNELS    4
#define BOARD_MIC_GAIN_DB     30.0f
```

- [ ] **Step 2: Extend `board_audio.h` with the input API**

Replace the body of `firmware/voice-assistant/main/board_audio.h` below the includes with:
```c
/* Brings up I2C, the speaker-amp (EXIO8), duplex I2S, the ES8311 output codec
   and the ES7210 input codec. Safe to call once at startup. */
esp_err_t board_audio_init(void);

/* The opened output codec device (ES8311), or NULL. */
esp_codec_dev_handle_t board_audio_get_output_dev(void);

/* The opened input codec device (ES7210), or NULL. */
esp_codec_dev_handle_t board_audio_get_input_dev(void);

/* Reads `frames` mono samples (channel 0 of the 4-ch ADC) into dst.
   Blocks until the I2S RX DMA has the data. Returns ESP_OK on success. */
esp_err_t board_audio_read_mono(int16_t *dst, size_t frames);
```

- [ ] **Step 3: Rewrite `board_audio.c` for duplex + ES7210**

Replace the entire contents of `firmware/voice-assistant/main/board_audio.c` with:
```c
#include "board_audio.h"
#include "board_config.h"

#include "esp_log.h"
#include "esp_check.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "driver/i2s_tdm.h"
#include "esp_io_expander_tca95xx_16bit.h"
#include "esp_codec_dev_defaults.h"

static const char *TAG = "board_audio";

static i2c_master_bus_handle_t  s_i2c_bus;
static esp_io_expander_handle_t s_io_expander;
static i2s_chan_handle_t        s_tx_chan;
static i2s_chan_handle_t        s_rx_chan;
static const audio_codec_data_if_t *s_data_if;   /* shared by in + out */
static esp_codec_dev_handle_t   s_out_dev;
static esp_codec_dev_handle_t   s_in_dev;

static esp_err_t init_i2c(void)
{
    i2c_master_bus_config_t cfg = {
        .i2c_port = BOARD_I2C_PORT,
        .sda_io_num = BOARD_I2C_SDA_GPIO,
        .scl_io_num = BOARD_I2C_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    return i2c_new_master_bus(&cfg, &s_i2c_bus);
}

static esp_err_t init_amp(void)
{
    ESP_RETURN_ON_ERROR(
        esp_io_expander_new_i2c_tca95xx_16bit(
            s_i2c_bus, ESP_IO_EXPANDER_I2C_TCA9555_ADDRESS_000, &s_io_expander),
        TAG, "tca9555 create");
    ESP_RETURN_ON_ERROR(
        esp_io_expander_set_dir(s_io_expander, BOARD_AMP_EXIO_PIN, IO_EXPANDER_OUTPUT),
        TAG, "exio8 dir");
    ESP_RETURN_ON_ERROR(
        esp_io_expander_set_level(s_io_expander, BOARD_AMP_EXIO_PIN, 1),
        TAG, "exio8 high");
    ESP_LOGI(TAG, "speaker amp enabled (TCA9555 EXIO8 = high)");
    return ESP_OK;
}

static esp_err_t init_i2s_duplex(void)
{
    i2s_chan_config_t chan_cfg = {
        .id = I2S_NUM_0,
        .role = I2S_ROLE_MASTER,
        .dma_desc_num = 6,
        .dma_frame_num = 240,
        .auto_clear_after_cb = true,
    };
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, &s_tx_chan, &s_rx_chan), TAG, "i2s new chan");

    /* TX (to ES8311 / speaker): standard Philips, stereo 16-bit */
    i2s_std_config_t std_cfg = {
        .clk_cfg = {
            .sample_rate_hz = BOARD_AUDIO_SAMPLE_RATE,
            .clk_src = I2S_CLK_SRC_DEFAULT,
            .mclk_multiple = I2S_MCLK_MULTIPLE_256,
        },
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = BOARD_I2S_MCLK_GPIO,
            .bclk = BOARD_I2S_BCLK_GPIO,
            .ws   = BOARD_I2S_WS_GPIO,
            .dout = BOARD_I2S_DOUT_GPIO,
            .din  = I2S_GPIO_UNUSED,
        },
    };
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_tx_chan, &std_cfg), TAG, "i2s init std");

    /* RX (from ES7210 / mics): TDM 4-slot 16-bit (ES7210 is a 4-channel ADC) */
    i2s_tdm_config_t tdm_cfg = {
        .clk_cfg = {
            .sample_rate_hz = BOARD_AUDIO_SAMPLE_RATE,
            .clk_src = I2S_CLK_SRC_DEFAULT,
            .mclk_multiple = I2S_MCLK_MULTIPLE_256,
            .bclk_div = 8,
        },
        .slot_cfg = {
            .data_bit_width = I2S_DATA_BIT_WIDTH_16BIT,
            .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,
            .slot_mode = I2S_SLOT_MODE_STEREO,
            .slot_mask = I2S_TDM_SLOT0 | I2S_TDM_SLOT1 | I2S_TDM_SLOT2 | I2S_TDM_SLOT3,
            .ws_width = I2S_TDM_AUTO_WS_WIDTH,
            .ws_pol = false,
            .bit_shift = true,
            .left_align = false,
            .big_endian = false,
            .bit_order_lsb = false,
            .skip_mask = false,
            .total_slot = I2S_TDM_AUTO_SLOT_NUM,
        },
        .gpio_cfg = {
            .mclk = BOARD_I2S_MCLK_GPIO,
            .bclk = BOARD_I2S_BCLK_GPIO,
            .ws   = BOARD_I2S_WS_GPIO,
            .dout = I2S_GPIO_UNUSED,
            .din  = BOARD_I2S_DIN_GPIO,
        },
    };
    ESP_RETURN_ON_ERROR(i2s_channel_init_tdm_mode(s_rx_chan, &tdm_cfg), TAG, "i2s init tdm");

    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_tx_chan), TAG, "i2s tx enable");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_rx_chan), TAG, "i2s rx enable");
    return ESP_OK;
}

static esp_err_t init_data_if(void)
{
    const audio_codec_i2s_cfg_t i2s_cfg = {
        .port = I2S_NUM_0,
        .tx_handle = s_tx_chan,
        .rx_handle = s_rx_chan,
    };
    s_data_if = audio_codec_new_i2s_data(&i2s_cfg);
    return s_data_if ? ESP_OK : ESP_FAIL;
}

static esp_err_t init_es8311_output(void)
{
    const audio_codec_i2c_cfg_t i2c_cfg = {
        .port = BOARD_I2C_PORT,
        .addr = ES8311_CODEC_DEFAULT_ADDR,
        .bus_handle = s_i2c_bus,
    };
    const audio_codec_ctrl_if_t *ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    ESP_RETURN_ON_FALSE(ctrl_if, ESP_FAIL, TAG, "es8311 i2c ctrl");

    const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();
    ESP_RETURN_ON_FALSE(gpio_if, ESP_FAIL, TAG, "gpio if");

    es8311_codec_cfg_t es8311_cfg = {
        .ctrl_if = ctrl_if,
        .gpio_if = gpio_if,
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC,
        .pa_pin = GPIO_NUM_NC,
        .use_mclk = true,
        .hw_gain = { .pa_voltage = 5.0, .codec_dac_voltage = 3.3 },
    };
    const audio_codec_if_t *codec_if = es8311_codec_new(&es8311_cfg);
    ESP_RETURN_ON_FALSE(codec_if, ESP_FAIL, TAG, "es8311 new");

    esp_codec_dev_cfg_t dev_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
        .codec_if = codec_if,
        .data_if = s_data_if,
    };
    s_out_dev = esp_codec_dev_new(&dev_cfg);
    ESP_RETURN_ON_FALSE(s_out_dev, ESP_FAIL, TAG, "out dev new");

    esp_codec_dev_sample_info_t fs = {
        .bits_per_sample = 16,
        .channel = 1,
        .channel_mask = 0,
        .sample_rate = BOARD_AUDIO_SAMPLE_RATE,
        .mclk_multiple = 0,
    };
    ESP_RETURN_ON_ERROR(esp_codec_dev_open(s_out_dev, &fs), TAG, "out open");
    ESP_RETURN_ON_ERROR(esp_codec_dev_set_out_vol(s_out_dev, 60), TAG, "out vol");
    ESP_LOGI(TAG, "ES8311 output codec opened");
    return ESP_OK;
}

static esp_err_t init_es7210_input(void)
{
    const audio_codec_i2c_cfg_t i2c_cfg = {
        .port = BOARD_I2C_PORT,
        .addr = ES7210_CODEC_DEFAULT_ADDR,
        .bus_handle = s_i2c_bus,
    };
    const audio_codec_ctrl_if_t *ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    ESP_RETURN_ON_FALSE(ctrl_if, ESP_FAIL, TAG, "es7210 i2c ctrl");

    es7210_codec_cfg_t es7210_cfg = {
        .ctrl_if = ctrl_if,
        .mic_selected = ES7210_SEL_MIC1 | ES7210_SEL_MIC2 | ES7210_SEL_MIC3 | ES7210_SEL_MIC4,
    };
    const audio_codec_if_t *codec_if = es7210_codec_new(&es7210_cfg);
    ESP_RETURN_ON_FALSE(codec_if, ESP_FAIL, TAG, "es7210 new");

    esp_codec_dev_cfg_t dev_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_IN,
        .codec_if = codec_if,
        .data_if = s_data_if,
    };
    s_in_dev = esp_codec_dev_new(&dev_cfg);
    ESP_RETURN_ON_FALSE(s_in_dev, ESP_FAIL, TAG, "in dev new");

    esp_codec_dev_sample_info_t fs = {
        .bits_per_sample = 16,
        .channel = BOARD_MIC_CHANNELS,
        .channel_mask = ESP_CODEC_DEV_MAKE_CHANNEL_MASK(0),
        .sample_rate = BOARD_AUDIO_SAMPLE_RATE,
        .mclk_multiple = 0,
    };
    ESP_RETURN_ON_ERROR(esp_codec_dev_open(s_in_dev, &fs), TAG, "in open");
    ESP_RETURN_ON_ERROR(
        esp_codec_dev_set_in_channel_gain(s_in_dev, ESP_CODEC_DEV_MAKE_CHANNEL_MASK(0), BOARD_MIC_GAIN_DB),
        TAG, "in gain");
    ESP_LOGI(TAG, "ES7210 input codec opened (24kHz/16bit/%d-ch, +%.0fdB ch0)",
             BOARD_MIC_CHANNELS, BOARD_MIC_GAIN_DB);
    return ESP_OK;
}

esp_err_t board_audio_init(void)
{
    ESP_RETURN_ON_ERROR(init_i2c(), TAG, "i2c");
    ESP_RETURN_ON_ERROR(init_amp(), TAG, "amp");
    ESP_RETURN_ON_ERROR(init_i2s_duplex(), TAG, "i2s");
    ESP_RETURN_ON_ERROR(init_data_if(), TAG, "data_if");
    ESP_RETURN_ON_ERROR(init_es8311_output(), TAG, "es8311");
    ESP_RETURN_ON_ERROR(init_es7210_input(), TAG, "es7210");
    return ESP_OK;
}

esp_codec_dev_handle_t board_audio_get_output_dev(void) { return s_out_dev; }
esp_codec_dev_handle_t board_audio_get_input_dev(void)  { return s_in_dev; }

esp_err_t board_audio_read_mono(int16_t *dst, size_t frames)
{
    int16_t tmp[240 * BOARD_MIC_CHANNELS];
    size_t done = 0;
    while (done < frames) {
        size_t n = frames - done;
        if (n > 240) n = 240;
        esp_err_t err = esp_codec_dev_read(s_in_dev, tmp, n * BOARD_MIC_CHANNELS * sizeof(int16_t));
        if (err != ESP_OK) return err;
        for (size_t i = 0; i < n; i++) {
            dst[done + i] = tmp[i * BOARD_MIC_CHANNELS];   /* channel 0 */
        }
        done += n;
    }
    return ESP_OK;
}
```

- [ ] **Step 4: Replace `main.c` with an init-and-idle stub (no tone)**

`firmware/voice-assistant/main/main.c`:
```c
#include "esp_log.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "board_audio.h"

static const char *TAG = "app";

void app_main(void)
{
    ESP_LOGI(TAG, "voice-assistant boot: milestone 1b (capture)");
    ESP_ERROR_CHECK(board_audio_init());
    ESP_LOGI(TAG, "board_audio_init OK (mic ready)");
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
```

- [ ] **Step 5: Build, flash, observe** (ensure no `idf.py monitor` is holding the port)

```bash
cd ~/Desktop/career/wareshare/firmware/voice-assistant
get_idf && idf.py build && idf.py -p /dev/cu.usbmodem101 flash
python /tmp/claude/serial_reset_capture.py     # reuse the reset+capture helper
```
Expected in the log: `ES8311 output codec opened`, `ES7210 input codec opened (24kHz/16bit/4-ch, +30dB ch0)`, `board_audio_init OK (mic ready)`. **No** I²C error/timeout, no panic. (If `es7210 new`/`in open` fails, check the ES7210 address and the TDM config against reference doc §3.)

- [ ] **Step 6: Commit**

```bash
cd ~/Desktop/career/wareshare
git add firmware/voice-assistant
git commit -m "feat(fw): duplex I2S + ES7210 input codec (mic opens cleanly)"
```

---

## Task 2: Record 3 s and stream it as base64 over serial

**Goal:** capture mic channel 0 for 3 s and emit it as base64 text between markers, with a clear "speak now" prompt the Mac tool can surface.

**Files:**
- Modify: `firmware/voice-assistant/main/CMakeLists.txt`
- Modify: `firmware/voice-assistant/main/main.c`

- [ ] **Step 1: Add mbedtls to the component REQUIRES (for base64)**

Edit `firmware/voice-assistant/main/CMakeLists.txt` — change the `REQUIRES` line to:
```cmake
    REQUIRES esp_driver_gpio esp_driver_i2c esp_driver_i2s esp_codec_dev esp_io_expander_tca95xx_16bit mbedtls
```

- [ ] **Step 2: Rewrite `main.c` to record + dump**

`firmware/voice-assistant/main/main.c`:
```c
#include <stdio.h>
#include "esp_log.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/base64.h"
#include "board_audio.h"
#include "board_config.h"

static const char *TAG = "app";

#define CAP_SECONDS  3
#define FRAME_LEN    240                                   /* mono samples per chunk (10 ms) */
#define TOTAL_FRAMES (CAP_SECONDS * BOARD_AUDIO_SAMPLE_RATE)

void app_main(void)
{
    ESP_LOGI(TAG, "voice-assistant boot: milestone 1b (capture)");
    ESP_ERROR_CHECK(board_audio_init());

    /* Cue the human (the Mac tool echoes this line). */
    printf("---RECORD-START--- speak now for %d seconds\n", CAP_SECONDS);

    int16_t mono[FRAME_LEN];
    unsigned char b64[(FRAME_LEN * 2 + 2) / 3 * 4 + 4];     /* base64 of FRAME_LEN*2 bytes */

    printf("---PCM-BEGIN--- rate=%d ch=1 bits=16 samples=%d\n",
           BOARD_AUDIO_SAMPLE_RATE, TOTAL_FRAMES);

    int sent = 0;
    while (sent < TOTAL_FRAMES) {
        int n = TOTAL_FRAMES - sent;
        if (n > FRAME_LEN) n = FRAME_LEN;
        if (board_audio_read_mono(mono, n) != ESP_OK) {
            ESP_LOGE(TAG, "mic read failed");
            break;
        }
        size_t olen = 0;
        mbedtls_base64_encode(b64, sizeof(b64), &olen, (unsigned char *)mono, (size_t)n * 2);
        printf("%.*s\n", (int)olen, b64);
        sent += n;
    }
    printf("---PCM-END---\n");
    ESP_LOGI(TAG, "capture done (%d samples). Press RST to record again.", sent);

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
```

- [ ] **Step 3: Build, flash, and sanity-check the stream** (close any monitor first)

```bash
cd ~/Desktop/career/wareshare/firmware/voice-assistant
get_idf && idf.py build && idf.py -p /dev/cu.usbmodem101 flash
```
Then capture a few seconds of serial to confirm the markers and data appear:
```bash
python - <<'PY'
import serial, time
s = serial.Serial("/dev/cu.usbmodem101", 115200, timeout=0.5)
s.setDTR(False); s.setRTS(True); time.sleep(0.12); s.setRTS(False)   # reset -> restart capture
begin=end=False; lines=0; end_t=time.time()+8
while time.time()<end_t:
    ln=s.readline().decode(errors="replace").strip()
    if ln.startswith("---RECORD-START---"): print("cue:", ln)
    if ln.startswith("---PCM-BEGIN---"): begin=True; print("hdr:", ln); continue
    if ln.startswith("---PCM-END---"): end=True; break
    if begin: lines+=1
s.close()
print("begin:", begin, "end:", end, "base64 lines:", lines)
PY
```
Expected: prints the cue, an `hdr:` line with `rate=24000 ... samples=72000`, `begin: True end: True`, and a few hundred base64 lines. (No need to listen yet.)

- [ ] **Step 4: Commit**

```bash
cd ~/Desktop/career/wareshare
git add firmware/voice-assistant
git commit -m "feat(fw): record 3s of mic ch0 and stream as base64 over serial"
```

---

## Task 3: Mac-side capture tool → WAV → listen

**Goal:** the milestone deliverable — turn the serial stream into a playable WAV and confirm your speech is intelligible.

**Files:**
- Create: `tools/capture_mic.py`

- [ ] **Step 1: Create the capture tool**

`tools/capture_mic.py`:
```python
#!/usr/bin/env python3
"""Reset the board, read its base64 PCM dump over serial, write a WAV.

Usage: python tools/capture_mic.py [port] [out.wav]
Then play it (macOS):  afplay mic.wav
"""
import sys, time, base64, wave
import serial   # pip install pyserial

PORT = sys.argv[1] if len(sys.argv) > 1 else "/dev/cu.usbmodem101"
OUT  = sys.argv[2] if len(sys.argv) > 2 else "mic.wav"

ser = serial.Serial(PORT, 115200, timeout=1)
# Reset the board (RTS pulse) so it re-runs app_main and records fresh.
ser.setDTR(False); ser.setRTS(True); time.sleep(0.12); ser.setRTS(False)

rate, capturing, data = 24000, False, bytearray()
deadline = time.time() + 30
while time.time() < deadline:
    line = ser.readline().decode(errors="replace").strip()
    if not line:
        continue
    if line.startswith("---RECORD-START---"):
        print(">>> SPEAK NOW <<<", line)
        continue
    if line.startswith("---PCM-BEGIN---"):
        for tok in line.split():
            if tok.startswith("rate="):
                rate = int(tok.split("=")[1])
        capturing = True
        print("recording stream...", line)
        continue
    if line.startswith("---PCM-END---"):
        break
    if capturing:
        try:
            data += base64.b64decode(line)
        except Exception:
            pass   # skip any non-data line that slipped in
ser.close()

if not data:
    print("ERROR: no audio captured (is a monitor holding the port? is the board running 1b?)")
    sys.exit(1)

with wave.open(OUT, "wb") as w:
    w.setnchannels(1)
    w.setsampwidth(2)
    w.setframerate(rate)
    w.writeframes(bytes(data))

secs = len(data) / 2 / rate
print(f"wrote {OUT}: {len(data)} bytes, {secs:.1f}s @ {rate} Hz mono")
print(f"play it:  afplay {OUT}")
```

- [ ] **Step 2: Ensure pyserial is available**

```bash
python3 -c "import serial" 2>/dev/null && echo "pyserial OK" || pip3 install pyserial
```
Expected: `pyserial OK` (or a successful install).

- [ ] **Step 3: Run the capture and SPEAK** (close any `idf.py monitor` first)

```bash
cd ~/Desktop/career/wareshare
python3 tools/capture_mic.py
```
When you see `>>> SPEAK NOW <<<`, talk toward the board's mics for ~3 s ("testing one two three, the quick brown fox"). Expected: `wrote mic.wav: 144000 bytes, 3.0s @ 24000 Hz mono`.

- [ ] **Step 4: LISTEN (the real test)**

```bash
afplay mic.wav
```
Confirm your speech is **intelligible** (some hiss/quietness is fine for a raw single-mic capture).
- If silent / all zeros → mic path didn't capture; re-check Task 1's `ES7210 ... opened` log and that nothing else holds the port.
- If too quiet → raise `BOARD_MIC_GAIN_DB` (e.g. 30 → 37.5) in `board_config.h`, rebuild/flash, recapture.
- If clipped/distorted → lower `BOARD_MIC_GAIN_DB`.

- [ ] **Step 5: Record the result and commit**

Add a `## ✅ Milestone 1b` line to `DECISIONS.md` (intelligible? final gain? any tweaks), then:
```bash
cd ~/Desktop/career/wareshare
git add tools/capture_mic.py DECISIONS.md
git commit -m "feat(tools): mic capture-to-WAV over serial (Milestone 1b done)"
```

> ✅ **Milestone 1b complete when:** `mic.wav` plays back your speech intelligibly, the boot log shows ES7210 opening with no errors, and the result is recorded in `DECISIONS.md`.

---

## What this sets up for Milestone 2 (wake word)

`board_audio` now owns a working mic stream (`board_audio_read_mono`). Milestone 2 feeds that stream into ESP-SR's AFE (noise reduction + VAD) and WakeNet for the built-in wake word, lighting an LED on detect. The 4-ch read + channel-0 extraction is exactly the frame format AFE expects (mono 16 kHz/24 kHz); if AFE wants 16 kHz we can either reconfigure the codec sample rate or resample — to be decided in the Milestone 2 plan. No new audio bring-up needed.
