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
