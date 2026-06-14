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
