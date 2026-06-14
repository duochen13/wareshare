#include "wakenet.h"
#include "board_audio.h"

#include <stdlib.h>
#include <math.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "model_path.h"
#include "esp_afe_sr_models.h"
#include "esp_afe_config.h"

static const char *TAG = "wakenet";

static srmodel_list_t           *s_models;
static const esp_afe_sr_iface_t *s_afe;
static esp_afe_sr_data_t        *s_afe_data;
static wakenet_detected_cb_t     s_cb;

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
    int cnt = 0;
    double acc = 0;
    while (1) {
        if (board_audio_read_mono(buf, chunk) == ESP_OK) {
            s_afe->feed(s_afe_data, buf);
            long long ss = 0;
            for (int i = 0; i < chunk; i++) ss += (long long)buf[i] * buf[i];
            acc += (double)ss / chunk;
            if (++cnt >= 31) {                              /* ~1 s at 16k/512 */
                ESP_LOGI(TAG, "[diag] feed RMS ~%d", (int)sqrt(acc / cnt));
                acc = 0; cnt = 0;
            }
        } else {
            ESP_LOGW(TAG, "[diag] mic read error");
        }
    }
}

/* Pulls AFE results and reports wake-word detections. */
static void detect_task(void *arg)
{
    int last_vad = -1;
    while (1) {
        afe_fetch_result_t *res = s_afe->fetch_with_delay(s_afe_data, portMAX_DELAY);
        if (res == NULL || res->ret_value == ESP_FAIL) {
            continue;
        }
        if ((int)res->vad_state != last_vad) {              /* [diag] hear speech? */
            last_vad = (int)res->vad_state;
            ESP_LOGI(TAG, "[diag] VAD = %s",
                     res->vad_state == VAD_SPEECH ? "SPEECH" : "silence");
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
