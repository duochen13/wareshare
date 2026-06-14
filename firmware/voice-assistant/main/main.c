#include <stdio.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/base64.h"
#include "board_audio.h"
#include "board_config.h"

static const char *TAG = "app";

#define CAP_SECONDS  3
#define TOTAL_FRAMES (CAP_SECONDS * BOARD_AUDIO_SAMPLE_RATE)
#define DUMP_CHUNK   240                                   /* mono samples per base64 line */

void app_main(void)
{
    ESP_LOGI(TAG, "voice-assistant boot: milestone 1b (capture)");
    ESP_ERROR_CHECK(board_audio_init());

    int16_t *buf = malloc((size_t)TOTAL_FRAMES * sizeof(int16_t));   /* ~144 KB in internal RAM */
    if (!buf) {
        ESP_LOGE(TAG, "out of memory for %d-sample buffer", TOTAL_FRAMES);
        return;
    }

    /* Phase 1: record the whole clip in ONE continuous read — no serial I/O in the
       loop, so the I2S RX DMA never starves/overflows (that overflow was producing
       full-scale glitch bursts). board_audio_read_mono blocks in real time (~3 s). */
    printf("---RECORD-START--- speak now for %d seconds\n", CAP_SECONDS);
    esp_err_t err = board_audio_read_mono(buf, TOTAL_FRAMES);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mic read failed: %s", esp_err_to_name(err));
        free(buf);
        return;
    }

    /* Phase 2: dump the captured buffer as base64, paced so the USB-serial console
       doesn't drop bytes (host must keep up). */
    unsigned char b64[(DUMP_CHUNK * 2 + 2) / 3 * 4 + 4];
    printf("---PCM-BEGIN--- rate=%d ch=1 bits=16 samples=%d\n",
           BOARD_AUDIO_SAMPLE_RATE, TOTAL_FRAMES);
    for (int off = 0; off < TOTAL_FRAMES; off += DUMP_CHUNK) {
        int n = TOTAL_FRAMES - off;
        if (n > DUMP_CHUNK) n = DUMP_CHUNK;
        size_t olen = 0;
        mbedtls_base64_encode(b64, sizeof(b64), &olen,
                              (unsigned char *)(buf + off), (size_t)n * 2);
        printf("%.*s\n", (int)olen, b64);
        /* Pace the output: the USB-serial console drops TX bytes if the host can't
           drain fast enough. A flush + small delay keeps the stream lossless. */
        fflush(stdout);
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    printf("---PCM-END---\n");

    free(buf);
    ESP_LOGI(TAG, "capture done. Press RST to record again.");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
