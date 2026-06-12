#include <math.h>
#include "esp_log.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "board_audio.h"
#include "board_config.h"

static const char *TAG = "app";

#define TONE_HZ     440.0f
#define AMPLITUDE   3000      /* of int16 full-scale 32767; ~ -20 dBFS, quiet */
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
