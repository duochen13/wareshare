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
