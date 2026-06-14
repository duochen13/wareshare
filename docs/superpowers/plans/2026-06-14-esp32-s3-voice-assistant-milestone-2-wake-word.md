# ESP32-S3 Voice Assistant — Implementation Plan: Milestone 2 (Wake Word)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.
>
> **NOTE — hardware in the loop:** the detection tests (Tasks 2 & 3) need the board on `/dev/cu.usbmodem*` and a human to **say the wake word** and watch the log / LED. Build/flash steps are automatable.
>
> **⚠️ Carry-over gotchas:** (1) only ONE process may hold the serial port — close `idf.py monitor` before flashing. (2) the board's USB port re-enumerates/drops often — re-check `ls /dev/cu.*` and pass the right port to `-p`. (3) capture all audio in continuous reads; never block the feed loop on slow I/O.

**Goal:** Make the board detect a spoken wake word ("Hi ESP" by default) using ESP-SR (AFE noise-reduction + VAD → WakeNet) on the live ES7210 mic stream, and flash the on-board RGB LED green when it fires.

**Architecture:** Builds on `board_audio` (Milestones 1a/1b). We switch the codec to **16 kHz** (WakeNet's required rate), add the `espressif/esp-sr` component with a flashable **`model`** partition holding the selected WakeNet model, and enable **octal PSRAM** (AFE needs it). A new `wakenet` module loads the model, creates an AFE-SR instance, runs a **feed task** (mic → `afe->feed`) and a **detect task** (`afe->fetch_with_delay` → check `wakeup_state`), and calls a callback on detection. A tiny `led` module drives the WS2812 strip on GPIO 38 (green for 2 s on detect).

**Tech Stack:** ESP-IDF v5.5, C. New deps: `espressif/esp-sr` (AFE + WakeNet9), `espressif/led_strip` (WS2812). PSRAM: 8 MB octal (ESP32-S3R8). Wake word: built-in WakeNet model, default `wn9_hiesp` ("Hi ESP"); change via `idf.py menuconfig`.

**Source of truth:** XiaoZhi's verified `main/audio/wake_words/afe_wake_word.cc` (esp-sr usage) and its octal-PSRAM sdkconfig + `model` partition (both confirmed booting on this exact board in Milestone 0).

---

## File Structure

```
firmware/voice-assistant/
├── partitions.csv          # CREATE: factory app + "model" partition for WakeNet
├── sdkconfig.defaults      # MODIFY: PSRAM (octal), custom partition table, wake-word select
└── main/
    ├── board_config.h      # MODIFY: BOARD_AUDIO_SAMPLE_RATE 24000 -> 16000
    ├── idf_component.yml    # MODIFY: add espressif/esp-sr + espressif/led_strip
    ├── CMakeLists.txt       # MODIFY: add wakenet.c, led.c; REQUIRES esp-sr, led_strip, esp_timer
    ├── wakenet.h / .c      # CREATE: load model, AFE feed/detect tasks, detection callback
    ├── led.h / .c          # CREATE: WS2812 init + flash-green-on-wake (esp_timer one-shot)
    └── main.c              # REWRITE: init board_audio + led, start wakenet, LED on detect
```

---

## Task 1: ESP-SR project setup — PSRAM, model partition, 16 kHz, model loads

**Goal:** get the ESP-SR dependency, the `model` partition, octal PSRAM, and 16 kHz capture all in place, and confirm at runtime that the WakeNet model loads. No AFE yet.

**Files:**
- Create: `firmware/voice-assistant/partitions.csv`
- Modify: `firmware/voice-assistant/sdkconfig.defaults`
- Modify: `firmware/voice-assistant/main/board_config.h`
- Modify: `firmware/voice-assistant/main/idf_component.yml`
- Modify: `firmware/voice-assistant/main/CMakeLists.txt`
- Create: `firmware/voice-assistant/main/wakenet.h`, `firmware/voice-assistant/main/wakenet.c`
- Modify: `firmware/voice-assistant/main/main.c`

- [ ] **Step 1: Create the partition table with a `model` partition**

`firmware/voice-assistant/partitions.csv` (esp-sr auto-flashes `srmodels.bin` to the partition literally named `model`):
```csv
# Name,   Type, SubType, Offset,   Size
nvs,      data, nvs,     0x9000,   0x6000,
phy_init, data, phy,     0xf000,   0x1000,
factory,  app,  factory, 0x10000,  0x300000,
model,    data, spiffs,  ,         0x100000,
```

- [ ] **Step 2: Add PSRAM + partition + wake-word config to `sdkconfig.defaults`**

Append to `firmware/voice-assistant/sdkconfig.defaults`:
```
# Custom partition table (adds the "model" partition for WakeNet)
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"

# Octal PSRAM (ESP32-S3R8) — required by ESP-SR AFE (verified config from XiaoZhi)
CONFIG_SPIRAM=y
CONFIG_SPIRAM_MODE_OCT=y
CONFIG_SPIRAM_TYPE_AUTO=y
CONFIG_SPIRAM_SPEED_80M=y

# ESP-SR: select the "Hi ESP" WakeNet9 wake word
CONFIG_SR_WN_WN9_HIESP=y
```
> To use a different wake word later, run `idf.py menuconfig` → **ESP Speech Recognition** → select the WakeNet model (e.g. "Alexa", "Jarvis"), and disable MultiNet command recognition to keep the model partition small.

- [ ] **Step 3: Switch the codec to 16 kHz**

WakeNet/AFE only accept 16 kHz. Edit `firmware/voice-assistant/main/board_config.h`:
```c
#define BOARD_AUDIO_SAMPLE_RATE   16000
```
(was 24000. The whole duplex codec now runs at 16 kHz; playback/capture still work — TTS at 24 kHz is a later-milestone concern.)

- [ ] **Step 4: Add the esp-sr and led_strip dependencies**

Replace `firmware/voice-assistant/main/idf_component.yml` with:
```yaml
dependencies:
  idf:
    version: ">=5.4"
  espressif/esp_codec_dev:
    version: "1.5.10"
  espressif/esp_io_expander_tca95xx_16bit:
    version: "2.0.2"
  espressif/esp-sr:
    version: "2.3.1"
  espressif/led_strip:
    version: "^2.5.5"
```

- [ ] **Step 5: Add the new sources + components to the build**

Replace `firmware/voice-assistant/main/CMakeLists.txt` with:
```cmake
idf_component_register(
    SRCS "main.c" "board_audio.c" "wakenet.c" "led.c"
    INCLUDE_DIRS "."
    REQUIRES esp_driver_gpio esp_driver_i2c esp_driver_i2s esp_codec_dev
             esp_io_expander_tca95xx_16bit mbedtls esp-sr led_strip esp_timer
)
```

- [ ] **Step 6: Create the wakenet module header**

`firmware/voice-assistant/main/wakenet.h`:
```c
#pragma once
#include <stdbool.h>

/* Called (from the detect task) each time the wake word fires. Keep it quick. */
typedef void (*wakenet_detected_cb_t)(void);

/* Loads the WakeNet model, creates AFE, and starts the feed + detect tasks.
   board_audio_init() must have been called first. Returns true on success. */
bool wakenet_start(wakenet_detected_cb_t on_detected);
```

- [ ] **Step 7: Create a minimal `wakenet.c` that only loads + logs the model**

`firmware/voice-assistant/main/wakenet.c` (AFE/tasks added in Task 2 — this step just proves the model partition + esp_srmodel work):
```c
#include "wakenet.h"
#include "esp_log.h"
#include "model_path.h"
#include "esp_afe_sr_models.h"

static const char *TAG = "wakenet";

static srmodel_list_t *s_models;

bool wakenet_start(wakenet_detected_cb_t on_detected)
{
    (void)on_detected;   /* used in Task 2 */

    s_models = esp_srmodel_init("model");
    if (s_models == NULL || s_models->num <= 0) {
        ESP_LOGE(TAG, "no models found in 'model' partition (num=%d)",
                 s_models ? s_models->num : -1);
        return false;
    }
    for (int i = 0; i < s_models->num; i++) {
        ESP_LOGI(TAG, "model %d: %s", i, s_models->model_name[i]);
    }
    char *wn = esp_srmodel_filter(s_models, ESP_WN_PREFIX, NULL);
    if (wn == NULL) {
        ESP_LOGE(TAG, "no WakeNet model found");
        return false;
    }
    ESP_LOGI(TAG, "wakenet model = %s; words = %s",
             wn, esp_srmodel_get_wake_words(s_models, wn));
    return true;
}
```

- [ ] **Step 8: Create a stub `led.c`/`led.h` so the build links (filled in Task 3)**

`firmware/voice-assistant/main/led.h`:
```c
#pragma once
void led_init(void);
void led_indicate_wake(void);
```
`firmware/voice-assistant/main/led.c`:
```c
#include "led.h"
void led_init(void) {}
void led_indicate_wake(void) {}
```

- [ ] **Step 9: Wire `main.c` to init audio + start wakenet (model-load check)**

`firmware/voice-assistant/main/main.c`:
```c
#include "esp_log.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "board_audio.h"
#include "wakenet.h"
#include "led.h"

static const char *TAG = "app";

void app_main(void)
{
    ESP_LOGI(TAG, "voice-assistant boot: milestone 2 (wake word)");
    led_init();
    ESP_ERROR_CHECK(board_audio_init());
    if (!wakenet_start(led_indicate_wake)) {
        ESP_LOGE(TAG, "wakenet_start failed");
    }
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
```

- [ ] **Step 10: Build, flash (incl. model partition), verify model loads**

```bash
cd ~/Desktop/career/wareshare/firmware/voice-assistant
get_idf && idf.py set-target esp32s3 && idf.py build
idf.py -p /dev/cu.usbmodem101 flash       # also flashes srmodels.bin to the "model" partition
python /tmp/claude/serial_reset_capture.py
```
Expected in the log: a `heap_init` line showing **PSRAM** (a large external-RAM region, e.g. "8192 KiB"), `ES7210 input codec opened`, and `wakenet model = wn9_hiesp; words = hiesp` (or similar). **No** `no models found` error, no boot loop. (If PSRAM fails to init → boot loop: re-check the SPIRAM_MODE_OCT setting. If `no models found` → the `model` partition wasn't flashed; re-run `idf.py flash`.)

- [ ] **Step 11: Commit**

```bash
cd ~/Desktop/career/wareshare
git add firmware/voice-assistant
git commit -m "feat(fw): add ESP-SR (PSRAM + model partition, 16kHz); WakeNet model loads"
```

---

## Task 2: AFE + WakeNet detection (log on wake)

**Goal:** create the AFE-SR pipeline, feed it the live mic, and log when the wake word is detected.

**Files:**
- Modify: `firmware/voice-assistant/main/wakenet.c`

- [ ] **Step 1: Replace `wakenet.c` with the full feed/detect implementation**

`firmware/voice-assistant/main/wakenet.c`:
```c
#include "wakenet.h"
#include "board_audio.h"

#include <stdlib.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "model_path.h"
#include "esp_afe_sr_models.h"
#include "esp_afe_config.h"

static const char *TAG = "wakenet";

static srmodel_list_t         *s_models;
static const esp_afe_sr_iface_t *s_afe;
static esp_afe_sr_data_t       *s_afe_data;
static wakenet_detected_cb_t    s_cb;

/* Reads the mic in feed-sized chunks and feeds AFE. Never does slow I/O. */
static void feed_task(void *arg)
{
    int chunk = s_afe->get_feed_chunksize(s_afe_data);   /* samples per channel */
    int16_t *buf = malloc((size_t)chunk * sizeof(int16_t));
    if (!buf) {
        ESP_LOGE(TAG, "feed buffer alloc failed (%d samples)", chunk);
        vTaskDelete(NULL);
        return;
    }
    while (1) {
        if (board_audio_read_mono(buf, chunk) == ESP_OK) {
            s_afe->feed(s_afe_data, buf);
        }
    }
}

/* Pulls AFE results and reports wake-word detections. */
static void detect_task(void *arg)
{
    while (1) {
        afe_fetch_result_t *res = s_afe->fetch_with_delay(s_afe_data, portMAX_DELAY);
        if (res == NULL || res->ret_value == ESP_FAIL) {
            continue;
        }
        if (res->wakeup_state == WAKENET_DETECTED) {
            ESP_LOGI(TAG, "★ WAKE WORD DETECTED (model index %d)", res->wakenet_model_index);
            if (s_cb) {
                s_cb();
            }
        }
    }
}

bool wakenet_start(wakenet_detected_cb_t on_detected)
{
    s_cb = on_detected;

    s_models = esp_srmodel_init("model");
    if (s_models == NULL || s_models->num <= 0) {
        ESP_LOGE(TAG, "no models found in 'model' partition");
        return false;
    }
    char *wn = esp_srmodel_filter(s_models, ESP_WN_PREFIX, NULL);
    if (wn == NULL) {
        ESP_LOGE(TAG, "no WakeNet model found");
        return false;
    }
    ESP_LOGI(TAG, "wakenet model = %s; words = %s",
             wn, esp_srmodel_get_wake_words(s_models, wn));

    /* Single mic, no echo reference -> input format "M". AFE_TYPE_SR runs WakeNet. */
    afe_config_t *cfg = afe_config_init("M", s_models, AFE_TYPE_SR, AFE_MODE_HIGH_PERF);
    cfg->aec_init = false;                                 /* no reference channel */
    cfg->memory_alloc_mode = AFE_MEMORY_ALLOC_MORE_PSRAM;
    cfg->afe_perferred_core = 1;
    cfg->afe_perferred_priority = 5;

    s_afe = esp_afe_handle_from_config(cfg);
    s_afe_data = s_afe->create_from_config(cfg);
    if (s_afe_data == NULL) {
        ESP_LOGE(TAG, "AFE create failed (PSRAM/memory?)");
        return false;
    }

    int feed_sz  = s_afe->get_feed_chunksize(s_afe_data);
    int fetch_sz = s_afe->get_fetch_chunksize(s_afe_data);
    ESP_LOGI(TAG, "AFE ready: feed=%d fetch=%d samples; say the wake word!", feed_sz, fetch_sz);

    xTaskCreatePinnedToCore(feed_task,   "afe_feed",   4096, NULL, 5, NULL, 0);
    xTaskCreatePinnedToCore(detect_task, "afe_detect", 4096, NULL, 5, NULL, 1);
    return true;
}
```

- [ ] **Step 2: Build and flash**

```bash
cd ~/Desktop/career/wareshare/firmware/voice-assistant
get_idf && idf.py build && idf.py -p /dev/cu.usbmodem101 flash
```
Expected: builds clean; flash OK.

- [ ] **Step 3: DETECT (the real test)** — open a monitor and say the wake word

```bash
idf.py -p /dev/cu.usbmodem101 monitor      # Ctrl-] to exit
```
Watch for `AFE ready: feed=... fetch=...`. Then say **"Hi ESP"** clearly a few times toward the board. Expected: `★ WAKE WORD DETECTED (model index 1)` each time.
- If nothing fires → speak closer/louder; confirm the mic works (Milestone 1b) and that `feed=`/`fetch=` are non-zero. Try the exact phrase for the selected model.
- If it boots-loops or AFE create fails → PSRAM/memory; confirm Task 1's PSRAM log.

- [ ] **Step 4: Commit**

```bash
cd ~/Desktop/career/wareshare
git add firmware/voice-assistant
git commit -m "feat(fw): AFE + WakeNet wake-word detection (logs on detect)"
```

---

## Task 3: Light the RGB LED on detection

**Goal:** visible feedback — flash the on-board WS2812 strip (GPIO 38) green for 2 s when the wake word fires.

**Files:**
- Modify: `firmware/voice-assistant/main/led.c`
- Modify: `firmware/voice-assistant/main/board_config.h`

- [ ] **Step 1: Add the LED pin/count constants to `board_config.h`**

Append to `firmware/voice-assistant/main/board_config.h`:
```c

/* On-board WS2812 RGB strip */
#define BOARD_LED_GPIO        38
#define BOARD_LED_COUNT       6
```

- [ ] **Step 2: Implement `led.c` (WS2812 + 2 s one-shot off timer)**

`firmware/voice-assistant/main/led.c`:
```c
#include "led.h"
#include "board_config.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "led_strip.h"

static const char *TAG = "led";
static led_strip_handle_t s_strip;
static esp_timer_handle_t s_off_timer;

static void led_off_cb(void *arg)
{
    if (s_strip) led_strip_clear(s_strip);
}

void led_init(void)
{
    led_strip_config_t strip_cfg = {
        .strip_gpio_num = BOARD_LED_GPIO,
        .max_leds = BOARD_LED_COUNT,
        .led_pixel_format = LED_PIXEL_FORMAT_GRB,
        .led_model = LED_MODEL_WS2812,
        .flags.invert_out = false,
    };
    led_strip_rmt_config_t rmt_cfg = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
        .flags.with_dma = false,
    };
    if (led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &s_strip) != ESP_OK) {
        ESP_LOGE(TAG, "WS2812 init failed");
        s_strip = NULL;
        return;
    }
    led_strip_clear(s_strip);

    const esp_timer_create_args_t targs = { .callback = led_off_cb, .name = "led_off" };
    esp_timer_create(&targs, &s_off_timer);
    ESP_LOGI(TAG, "LED ready (WS2812 x%d on GPIO%d)", BOARD_LED_COUNT, BOARD_LED_GPIO);
}

void led_indicate_wake(void)
{
    if (!s_strip) return;
    for (int i = 0; i < BOARD_LED_COUNT; i++) {
        led_strip_set_pixel(s_strip, i, 0, 40, 0);   /* dim green */
    }
    led_strip_refresh(s_strip);
    esp_timer_stop(s_off_timer);                      /* re-arm if already lit */
    esp_timer_start_once(s_off_timer, 2000000);       /* 2 s, in microseconds */
}
```

- [ ] **Step 3: Build and flash**

```bash
cd ~/Desktop/career/wareshare/firmware/voice-assistant
get_idf && idf.py build && idf.py -p /dev/cu.usbmodem101 flash
```
Expected: builds clean; flash OK. (If `led_strip_new_rmt_device`/struct fields error, the installed `led_strip` is a different major version — check `managed_components/espressif__led_strip/include/led_strip*.h` for the field names and adjust.)

- [ ] **Step 4: VERIFY (human)** — say the wake word, watch the LED

Power the board (a monitor is optional now). Say **"Hi ESP"**. Expected: the RGB LED turns **green for ~2 seconds**, then off, each time the wake word is detected.
- LED never lights but the log (Task 2) shows detections → GPIO/strip issue; check `LED ready` logged at boot and the GPIO number.
- LED stuck on → fine if you keep triggering; it re-arms the 2 s timer each detection.

- [ ] **Step 5: Record the result and commit**

Add a `## ✅ Milestone 2` line to `DECISIONS.md` (wake word used, detection reliability, any tuning), then:
```bash
cd ~/Desktop/career/wareshare
git add firmware/voice-assistant DECISIONS.md
git commit -m "feat(fw): flash RGB LED green on wake-word detect (Milestone 2 done)"
```

> ✅ **Milestone 2 complete when:** saying the wake word reliably logs `★ WAKE WORD DETECTED` and flashes the LED green, in a quiet room, and the result is recorded in `DECISIONS.md`.

---

## What this sets up for Milestone 3 (server + connectivity)

The board now has the full local audio loop: mic → AFE/VAD → wake detect, plus speaker (1a). Milestone 3 adds Wi-Fi (NVS-stored credentials) and a Python WebSocket server on the Mac (mDNS discovery), so that after the wake word the board can stream audio to the server and back. AFE's VAD (`res->vad_state` / `vad_cache`) will also give us the end-of-speech endpoint we need for Milestone 4 (Whisper) — no new audio bring-up required.
