# ESP32-S3 Voice Assistant — Implementation Plan: Milestone 1a (Audio Playback)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.
>
> **NOTE — hardware in the loop:** the final verification of every task is *flash + observe*. Software steps (write code, build) are automatable; flashing needs the board on `/dev/cu.usbmodem101` and a human to **listen for the tone** in Task 4. If the port is missing, run `ls /dev/cu.*` (it can vanish on unplug/reset).

**Goal:** Build our *own* ESP-IDF firmware that plays a clean 440 Hz sine tone out of the speaker (ES8311 DAC → NS4150B amp) at 24 kHz. Done when you hear a steady tone.

**Architecture:** A fresh ESP-IDF project at `firmware/voice-assistant/`. A `board_audio` module owns hardware bring-up: I²C master bus → TCA9555 IO-expander (drive EXIO8 high to enable the speaker amp) → I²S TX channel → ES8311 output codec via the standard `esp_codec_dev` component. `app_main` generates a sine wave and streams it with `esp_codec_dev_write`. We reuse Espressif's codec/expander drivers (we own the app logic, not the chip drivers) and copy the **exact init sequence already verified on this board** in Milestone 0.

**Tech Stack:** ESP-IDF v5.5, C. Managed components (verified-good versions): `espressif/esp_codec_dev` 1.5.10, `espressif/esp_io_expander_tca95xx_16bit` 2.0.2. Board: Waveshare ESP32-S3-AUDIO-Board (16 MB flash, ES8311 DAC, ES7210 ADC, NS4150B amp).

**Source of truth for pins/init:** `docs/superpowers/reference/xiaozhi-board-reference.md` and `DECISIONS.md` (pin map hardware-confirmed in Milestone 0).

---

## File Structure (created in this milestone)

```
firmware/voice-assistant/
├── CMakeLists.txt              # project file
├── sdkconfig.defaults          # target esp32s3, 16MB flash, log level
└── main/
    ├── CMakeLists.txt          # component registration + REQUIRES
    ├── idf_component.yml        # managed dependencies (esp_codec_dev, tca95xx)
    ├── board_config.h          # the verified pin map (constants only)
    ├── board_audio.h           # public API: init + get output device
    ├── board_audio.c           # I2C, TCA9555/amp, I2S, ES8311 bring-up
    └── main.c                  # app_main: sine generator + playback loop
```

Each task ends with a build, a flash, an observation, and a commit.

---

## Task 1: Project skeleton that builds, flashes, and boots

**Files:**
- Create: `firmware/voice-assistant/CMakeLists.txt`
- Create: `firmware/voice-assistant/sdkconfig.defaults`
- Create: `firmware/voice-assistant/main/CMakeLists.txt`
- Create: `firmware/voice-assistant/main/idf_component.yml`
- Create: `firmware/voice-assistant/main/main.c`

- [ ] **Step 1: Create the project `CMakeLists.txt`**

`firmware/voice-assistant/CMakeLists.txt`:
```cmake
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(voice_assistant)
```

- [ ] **Step 2: Create `sdkconfig.defaults`**

`firmware/voice-assistant/sdkconfig.defaults` (16 MB flash to match the board; NO camera configs — our board has no camera):
```
CONFIG_IDF_TARGET="esp32s3"
CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y
CONFIG_ESPTOOLPY_FLASHSIZE="16MB"
CONFIG_LOG_DEFAULT_LEVEL_INFO=y
CONFIG_FREERTOS_HZ=1000
```

- [ ] **Step 3: Create the managed-dependency manifest**

`firmware/voice-assistant/main/idf_component.yml` (pinned to the versions verified in Milestone 0):
```yaml
dependencies:
  idf:
    version: ">=5.4"
  espressif/esp_codec_dev:
    version: "1.5.10"
  espressif/esp_io_expander_tca95xx_16bit:
    version: "2.0.2"
```

- [ ] **Step 4: Create the main component `CMakeLists.txt`**

`firmware/voice-assistant/main/CMakeLists.txt` (only `main.c` for now; the other sources are added in Task 2/4):
```cmake
idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES esp_driver_i2c esp_driver_i2s esp_codec_dev esp_io_expander_tca95xx_16bit
)
```

- [ ] **Step 5: Create a placeholder `main.c`**

`firmware/voice-assistant/main/main.c`:
```c
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "app";

void app_main(void)
{
    ESP_LOGI(TAG, "voice-assistant boot: milestone 1a skeleton");
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
```

- [ ] **Step 6: Set target and build**

Run:
```bash
cd ~/Desktop/career/wareshare/firmware/voice-assistant
get_idf && idf.py set-target esp32s3 && idf.py build
```
Expected: ends with "Project build complete." The component manager downloads `esp_codec_dev` and `esp_io_expander_tca95xx_16bit` (needs network). If a download fails, re-run `idf.py build`.

- [ ] **Step 7: Flash and confirm boot**

Run:
```bash
idf.py -p /dev/cu.usbmodem101 flash
python /tmp/claude/serial_reset_capture.py   # or: idf.py -p /dev/cu.usbmodem101 monitor  (Ctrl-] to exit)
```
Expected (in the serial log): `voice-assistant boot: milestone 1a skeleton`, no panic/reboot loop.

- [ ] **Step 8: Commit**

```bash
cd ~/Desktop/career/wareshare
git add firmware/voice-assistant
git commit -m "feat(fw): scaffold voice-assistant ESP-IDF project (boots on board)"
```

---

## Task 2: Board bring-up — I²C bus, TCA9555, enable the speaker amp

**Goal:** create the `board_audio` module and bring up I²C + the IO-expander, driving **EXIO8 high** (the #1 gotcha — without it the speaker is silent). No audio yet; we just prove the expander is reachable.

**Files:**
- Create: `firmware/voice-assistant/main/board_config.h`
- Create: `firmware/voice-assistant/main/board_audio.h`
- Create: `firmware/voice-assistant/main/board_audio.c`
- Modify: `firmware/voice-assistant/main/CMakeLists.txt` (add `board_audio.c`)
- Modify: `firmware/voice-assistant/main/main.c` (call the init)

- [ ] **Step 1: Create the pin map header**

`firmware/voice-assistant/main/board_config.h` (values from the hardware-confirmed reference doc):
```c
#pragma once
#include "driver/gpio.h"
#include "esp_io_expander.h"

/* I2C control bus (codec + IO expander share it) */
#define BOARD_I2C_PORT        0
#define BOARD_I2C_SDA_GPIO    GPIO_NUM_11
#define BOARD_I2C_SCL_GPIO    GPIO_NUM_10

/* I2S audio bus */
#define BOARD_I2S_MCLK_GPIO   GPIO_NUM_12
#define BOARD_I2S_BCLK_GPIO   GPIO_NUM_13
#define BOARD_I2S_WS_GPIO     GPIO_NUM_14
#define BOARD_I2S_DOUT_GPIO   GPIO_NUM_16   /* to ES8311 / speaker */
#define BOARD_I2S_DIN_GPIO    GPIO_NUM_15   /* from ES7210 / mics (unused in 1a) */

/* Speaker amplifier enable is NOT a GPIO: it is EXIO8 on the TCA9555 expander */
#define BOARD_AMP_EXIO_PIN    IO_EXPANDER_PIN_NUM_8

#define BOARD_AUDIO_SAMPLE_RATE   24000
```

- [ ] **Step 2: Create the module header**

`firmware/voice-assistant/main/board_audio.h`:
```c
#pragma once
#include "esp_err.h"
#include "esp_codec_dev.h"

/* Brings up I2C, the speaker-amp (EXIO8), I2S and the ES8311 output codec.
   Safe to call once at startup. */
esp_err_t board_audio_init(void);

/* Returns the opened output codec device, or NULL if init failed/not called. */
esp_codec_dev_handle_t board_audio_get_output_dev(void);
```

- [ ] **Step 3: Create `board_audio.c` with I²C + amp bring-up only**

`firmware/voice-assistant/main/board_audio.c`:
```c
#include "board_audio.h"
#include "board_config.h"

#include "esp_log.h"
#include "esp_check.h"
#include "driver/i2c_master.h"
#include "esp_io_expander_tca95xx_16bit.h"

static const char *TAG = "board_audio";

static i2c_master_bus_handle_t s_i2c_bus;
static esp_io_expander_handle_t s_io_expander;
static esp_codec_dev_handle_t  s_out_dev;   /* filled in Task 3 */

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

esp_err_t board_audio_init(void)
{
    ESP_RETURN_ON_ERROR(init_i2c(), TAG, "i2c");
    ESP_RETURN_ON_ERROR(init_amp(), TAG, "amp");
    /* I2S + ES8311 are added in Task 3 */
    return ESP_OK;
}

esp_codec_dev_handle_t board_audio_get_output_dev(void)
{
    return s_out_dev;
}
```

- [ ] **Step 4: Add `board_audio.c` to the build**

Edit `firmware/voice-assistant/main/CMakeLists.txt` — change the `SRCS` line to:
```cmake
    SRCS "main.c" "board_audio.c"
```

- [ ] **Step 5: Call the init from `main.c`**

Replace `firmware/voice-assistant/main/main.c` with:
```c
#include "esp_log.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "board_audio.h"

static const char *TAG = "app";

void app_main(void)
{
    ESP_LOGI(TAG, "voice-assistant boot: milestone 1a");
    ESP_ERROR_CHECK(board_audio_init());
    ESP_LOGI(TAG, "board_audio_init OK");
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
```

- [ ] **Step 6: Build, flash, observe**

Run:
```bash
cd ~/Desktop/career/wareshare/firmware/voice-assistant
get_idf && idf.py build && idf.py -p /dev/cu.usbmodem101 flash
python /tmp/claude/serial_reset_capture.py
```
Expected in the log: `speaker amp enabled (TCA9555 EXIO8 = high)` then `board_audio_init OK`, and **no** `tca9555 create` error and no I²C timeout. (A failure here means the I²C pins/address are wrong — revisit `board_config.h` against the reference doc.)

- [ ] **Step 7: Commit**

```bash
cd ~/Desktop/career/wareshare
git add firmware/voice-assistant
git commit -m "feat(fw): bring up I2C + TCA9555, enable speaker amp (EXIO8)"
```

---

## Task 3: I²S TX channel + ES8311 output codec

**Goal:** create the I²S TX channel and the ES8311 output device via `esp_codec_dev`, and open it for 24 kHz / 16-bit / mono. Still no tone; we prove the codec opens cleanly.

**Files:**
- Modify: `firmware/voice-assistant/main/board_audio.c` (add I²S + ES8311)

- [ ] **Step 1: Add includes and a TX-channel helper**

In `firmware/voice-assistant/main/board_audio.c`, add to the include block:
```c
#include "driver/i2s_std.h"
#include "esp_codec_dev_defaults.h"
```
Add a static handle near the other statics:
```c
static i2s_chan_handle_t s_tx_chan;
```
Add this function above `board_audio_init`:
```c
static esp_err_t init_i2s_tx(void)
{
    i2s_chan_config_t chan_cfg = {
        .id = I2S_NUM_0,
        .role = I2S_ROLE_MASTER,
        .dma_desc_num = 6,
        .dma_frame_num = 240,
        .auto_clear_after_cb = true,
    };
    /* TX only for playback; no RX handle in milestone 1a */
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, &s_tx_chan, NULL), TAG, "i2s new chan");

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
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_tx_chan), TAG, "i2s enable");
    return ESP_OK;
}
```

- [ ] **Step 2: Add the ES8311 codec helper**

Add this function above `board_audio_init` (uses the exact ES8311 config verified in Milestone 0; `pa_pin = GPIO_NUM_NC` because the amp is on the expander, already driven in Task 2):
```c
static esp_err_t init_es8311_output(void)
{
    const audio_codec_i2s_cfg_t i2s_cfg = {
        .port = I2S_NUM_0,
        .tx_handle = s_tx_chan,
        .rx_handle = NULL,
    };
    const audio_codec_data_if_t *data_if = audio_codec_new_i2s_data(&i2s_cfg);
    ESP_RETURN_ON_FALSE(data_if, ESP_FAIL, TAG, "i2s data if");

    const audio_codec_i2c_cfg_t i2c_cfg = {
        .port = BOARD_I2C_PORT,
        .addr = ES8311_CODEC_DEFAULT_ADDR,
        .bus_handle = s_i2c_bus,
    };
    const audio_codec_ctrl_if_t *ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    ESP_RETURN_ON_FALSE(ctrl_if, ESP_FAIL, TAG, "i2c ctrl if");

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
        .data_if = data_if,
    };
    s_out_dev = esp_codec_dev_new(&dev_cfg);
    ESP_RETURN_ON_FALSE(s_out_dev, ESP_FAIL, TAG, "codec dev new");

    esp_codec_dev_sample_info_t fs = {
        .bits_per_sample = 16,
        .channel = 1,
        .channel_mask = 0,
        .sample_rate = BOARD_AUDIO_SAMPLE_RATE,
        .mclk_multiple = 0,
    };
    ESP_RETURN_ON_ERROR(esp_codec_dev_open(s_out_dev, &fs), TAG, "codec open");
    ESP_RETURN_ON_ERROR(esp_codec_dev_set_out_vol(s_out_dev, 80), TAG, "set vol");
    ESP_LOGI(TAG, "ES8311 output codec opened (24kHz/16bit/mono, vol 80)");
    return ESP_OK;
}
```

- [ ] **Step 3: Wire the two helpers into `board_audio_init`**

Change `board_audio_init` body to:
```c
esp_err_t board_audio_init(void)
{
    ESP_RETURN_ON_ERROR(init_i2c(), TAG, "i2c");
    ESP_RETURN_ON_ERROR(init_amp(), TAG, "amp");
    ESP_RETURN_ON_ERROR(init_i2s_tx(), TAG, "i2s");
    ESP_RETURN_ON_ERROR(init_es8311_output(), TAG, "es8311");
    return ESP_OK;
}
```

- [ ] **Step 4: Build, flash, observe**

Run:
```bash
cd ~/Desktop/career/wareshare/firmware/voice-assistant
get_idf && idf.py build && idf.py -p /dev/cu.usbmodem101 flash
python /tmp/claude/serial_reset_capture.py
```
Expected in the log: an `ES8311` line from the driver, then `ES8311 output codec opened (24kHz/16bit/mono, vol 80)` and `board_audio_init OK`. **No** I²C error/timeout, no assert/panic. (If `codec open` fails, the I²S clock or ES8311 address is wrong — compare against the reference doc §3.)

- [ ] **Step 5: Commit**

```bash
cd ~/Desktop/career/wareshare
git add firmware/voice-assistant
git commit -m "feat(fw): add I2S TX + ES8311 output codec (opens cleanly)"
```

---

## Task 4: Generate and play a 440 Hz sine tone

**Goal:** the milestone deliverable — a steady, clean tone from the speaker.

**Files:**
- Modify: `firmware/voice-assistant/main/main.c` (sine generator + write loop)

- [ ] **Step 1: Replace `main.c` with the playback loop**

`firmware/voice-assistant/main/main.c`:
```c
#include <math.h>
#include "esp_log.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "board_audio.h"
#include "board_config.h"

static const char *TAG = "app";

#define TONE_HZ     440.0f
#define AMPLITUDE   8000      /* of int16 full-scale 32767; ~ -12 dBFS, safe */
#define FRAME_LEN   240       /* mono samples per write (10 ms at 24 kHz) */

void app_main(void)
{
    ESP_LOGI(TAG, "voice-assistant boot: milestone 1a (tone)");
    ESP_ERROR_CHECK(board_audio_init());
    esp_codec_dev_handle_t dev = board_audio_get_output_dev();
    ESP_LOGI(TAG, "playing %.0f Hz sine; you should hear a steady tone", TONE_HZ);

    int16_t frame[FRAME_LEN];
    float phase = 0.0f;
    const float dphi = 2.0f * (float)M_PI * TONE_HZ / (float)BOARD_AUDIO_SAMPLE_RATE;

    while (1) {
        for (int i = 0; i < FRAME_LEN; i++) {
            frame[i] = (int16_t)(AMPLITUDE * sinf(phase));
            phase += dphi;
            if (phase >= 2.0f * (float)M_PI) {
                phase -= 2.0f * (float)M_PI;
            }
        }
        esp_err_t err = esp_codec_dev_write(dev, frame, sizeof(frame));
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "codec write failed: %s", esp_err_to_name(err));
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}
```

- [ ] **Step 2: Build and flash**

Run:
```bash
cd ~/Desktop/career/wareshare/firmware/voice-assistant
get_idf && idf.py build && idf.py -p /dev/cu.usbmodem101 flash
```
Expected: build complete, flash verified, hard reset.

- [ ] **Step 3: LISTEN (the real test)**

With the board's speaker audible, confirm you hear a **steady, clean 440 Hz tone** (a clear musical "A"). Optionally watch the log for `playing 440 Hz sine ...` and absence of `codec write failed`.
- If silent but the log is clean → the amp (EXIO8) didn't latch; power-cycle and re-check Task 2's log line.
- If buzzy/distorted → lower `AMPLITUDE` (e.g. 4000) and re-flash.

- [ ] **Step 4: Record the result and commit**

Add a one-line result to `DECISIONS.md` under a new `## ✅ Milestone 1a` heading (did you hear the tone? any amplitude/volume tweaks?), then:
```bash
cd ~/Desktop/career/wareshare
git add firmware/voice-assistant DECISIONS.md
git commit -m "feat(fw): play 440 Hz sine tone through ES8311 (Milestone 1a done)"
```

> ✅ **Milestone 1a complete when:** you hear a steady, clean tone from the speaker, the serial log shows the bring-up lines with no I²C/codec errors, and the result is recorded in `DECISIONS.md`.

---

## What this sets up for Milestone 1b (capture)

`board_audio` already creates the I²C bus and owns `I2S_NUM_0`. Milestone 1b (mic capture) will add an **RX** path: create a duplex channel instead of TX-only (`i2s_new_channel(&chan_cfg, &s_tx_chan, &s_rx_chan)`), configure it as **TDM 4-slot** (the ES7210 is a 4-channel ADC — see reference doc §3), add the ES7210 input device (`es7210_codec_new`), and read frames keeping channel 0. No new hardware bring-up needed.
