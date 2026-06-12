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
