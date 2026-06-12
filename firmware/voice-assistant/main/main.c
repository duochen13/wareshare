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
