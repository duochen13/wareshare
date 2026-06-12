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
