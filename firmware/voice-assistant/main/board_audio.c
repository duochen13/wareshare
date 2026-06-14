#include "board_audio.h"
#include "board_config.h"

#include "esp_log.h"
#include "esp_check.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "driver/i2s_tdm.h"
#include "esp_io_expander_tca95xx_16bit.h"
#include "esp_codec_dev_defaults.h"

static const char *TAG = "board_audio";

static i2c_master_bus_handle_t  s_i2c_bus;
static esp_io_expander_handle_t s_io_expander;
static i2s_chan_handle_t        s_tx_chan;
static i2s_chan_handle_t        s_rx_chan;
static const audio_codec_data_if_t *s_data_if;   /* shared by in + out */
static esp_codec_dev_handle_t   s_out_dev;
static esp_codec_dev_handle_t   s_in_dev;

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

static esp_err_t init_i2s_duplex(void)
{
    i2s_chan_config_t chan_cfg = {
        .id = I2S_NUM_0,
        .role = I2S_ROLE_MASTER,
        .dma_desc_num = 6,
        .dma_frame_num = 240,
        .auto_clear_after_cb = true,
    };
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, &s_tx_chan, &s_rx_chan), TAG, "i2s new chan");

    /* TX (to ES8311 / speaker): standard Philips, stereo 16-bit */
    i2s_std_config_t std_cfg = {
        .clk_cfg = {
            .sample_rate_hz = BOARD_AUDIO_SAMPLE_RATE,
            .clk_src = I2S_CLK_SRC_DEFAULT,
            .mclk_multiple = I2S_MCLK_MULTIPLE_256,
        },
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = BOARD_I2S_MCLK_GPIO,
            .bclk = BOARD_I2S_BCLK_GPIO,
            .ws   = BOARD_I2S_WS_GPIO,
            .dout = BOARD_I2S_DOUT_GPIO,
            .din  = I2S_GPIO_UNUSED,
        },
    };
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_tx_chan, &std_cfg), TAG, "i2s init std");

    /* RX (from ES7210 / mics): TDM 4-slot 16-bit (ES7210 is a 4-channel ADC) */
    i2s_tdm_config_t tdm_cfg = {
        .clk_cfg = {
            .sample_rate_hz = BOARD_AUDIO_SAMPLE_RATE,
            .clk_src = I2S_CLK_SRC_DEFAULT,
            .mclk_multiple = I2S_MCLK_MULTIPLE_256,
            .bclk_div = 8,
        },
        .slot_cfg = {
            .data_bit_width = I2S_DATA_BIT_WIDTH_16BIT,
            .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,
            .slot_mode = I2S_SLOT_MODE_STEREO,
            .slot_mask = I2S_TDM_SLOT0 | I2S_TDM_SLOT1 | I2S_TDM_SLOT2 | I2S_TDM_SLOT3,
            .ws_width = I2S_TDM_AUTO_WS_WIDTH,
            .ws_pol = false,
            .bit_shift = true,
            .left_align = false,
            .big_endian = false,
            .bit_order_lsb = false,
            .skip_mask = false,
            .total_slot = I2S_TDM_AUTO_SLOT_NUM,
        },
        .gpio_cfg = {
            .mclk = BOARD_I2S_MCLK_GPIO,
            .bclk = BOARD_I2S_BCLK_GPIO,
            .ws   = BOARD_I2S_WS_GPIO,
            .dout = I2S_GPIO_UNUSED,
            .din  = BOARD_I2S_DIN_GPIO,
        },
    };
    ESP_RETURN_ON_ERROR(i2s_channel_init_tdm_mode(s_rx_chan, &tdm_cfg), TAG, "i2s init tdm");

    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_tx_chan), TAG, "i2s tx enable");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_rx_chan), TAG, "i2s rx enable");
    return ESP_OK;
}

static esp_err_t init_data_if(void)
{
    const audio_codec_i2s_cfg_t i2s_cfg = {
        .port = I2S_NUM_0,
        .tx_handle = s_tx_chan,
        .rx_handle = s_rx_chan,
    };
    s_data_if = audio_codec_new_i2s_data(&i2s_cfg);
    return s_data_if ? ESP_OK : ESP_FAIL;
}

static esp_err_t init_es8311_output(void)
{
    const audio_codec_i2c_cfg_t i2c_cfg = {
        .port = BOARD_I2C_PORT,
        .addr = ES8311_CODEC_DEFAULT_ADDR,
        .bus_handle = s_i2c_bus,
    };
    const audio_codec_ctrl_if_t *ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    ESP_RETURN_ON_FALSE(ctrl_if, ESP_FAIL, TAG, "es8311 i2c ctrl");

    const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();
    ESP_RETURN_ON_FALSE(gpio_if, ESP_FAIL, TAG, "gpio if");

    es8311_codec_cfg_t es8311_cfg = {
        .ctrl_if = ctrl_if,
        .gpio_if = gpio_if,
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC,
        .pa_pin = GPIO_NUM_NC,
        .use_mclk = true,
        .hw_gain = { .pa_voltage = 5.0, .codec_dac_voltage = 3.3 },
    };
    const audio_codec_if_t *codec_if = es8311_codec_new(&es8311_cfg);
    ESP_RETURN_ON_FALSE(codec_if, ESP_FAIL, TAG, "es8311 new");

    esp_codec_dev_cfg_t dev_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
        .codec_if = codec_if,
        .data_if = s_data_if,
    };
    s_out_dev = esp_codec_dev_new(&dev_cfg);
    ESP_RETURN_ON_FALSE(s_out_dev, ESP_FAIL, TAG, "out dev new");

    esp_codec_dev_sample_info_t fs = {
        .bits_per_sample = 16,
        .channel = 1,
        .channel_mask = 0,
        .sample_rate = BOARD_AUDIO_SAMPLE_RATE,
        .mclk_multiple = 0,
    };
    ESP_RETURN_ON_ERROR(esp_codec_dev_open(s_out_dev, &fs), TAG, "out open");
    ESP_RETURN_ON_ERROR(esp_codec_dev_set_out_vol(s_out_dev, 60), TAG, "out vol");
    ESP_LOGI(TAG, "ES8311 output codec opened");
    return ESP_OK;
}

static esp_err_t init_es7210_input(void)
{
    const audio_codec_i2c_cfg_t i2c_cfg = {
        .port = BOARD_I2C_PORT,
        .addr = ES7210_CODEC_DEFAULT_ADDR,
        .bus_handle = s_i2c_bus,
    };
    const audio_codec_ctrl_if_t *ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    ESP_RETURN_ON_FALSE(ctrl_if, ESP_FAIL, TAG, "es7210 i2c ctrl");

    es7210_codec_cfg_t es7210_cfg = {
        .ctrl_if = ctrl_if,
        .mic_selected = ES7210_SEL_MIC1 | ES7210_SEL_MIC2 | ES7210_SEL_MIC3 | ES7210_SEL_MIC4,
    };
    const audio_codec_if_t *codec_if = es7210_codec_new(&es7210_cfg);
    ESP_RETURN_ON_FALSE(codec_if, ESP_FAIL, TAG, "es7210 new");

    esp_codec_dev_cfg_t dev_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_IN,
        .codec_if = codec_if,
        .data_if = s_data_if,
    };
    s_in_dev = esp_codec_dev_new(&dev_cfg);
    ESP_RETURN_ON_FALSE(s_in_dev, ESP_FAIL, TAG, "in dev new");

    esp_codec_dev_sample_info_t fs = {
        .bits_per_sample = 16,
        .channel = BOARD_MIC_CHANNELS,
        .channel_mask = ESP_CODEC_DEV_MAKE_CHANNEL_MASK(0),
        .sample_rate = BOARD_AUDIO_SAMPLE_RATE,
        .mclk_multiple = 0,
    };
    ESP_RETURN_ON_ERROR(esp_codec_dev_open(s_in_dev, &fs), TAG, "in open");
    ESP_RETURN_ON_ERROR(
        esp_codec_dev_set_in_channel_gain(s_in_dev, ESP_CODEC_DEV_MAKE_CHANNEL_MASK(0), BOARD_MIC_GAIN_DB),
        TAG, "in gain");
    ESP_LOGI(TAG, "ES7210 input codec opened (%dHz/16bit/%d-ch, +%.0fdB ch0)",
             BOARD_AUDIO_SAMPLE_RATE, BOARD_MIC_CHANNELS, BOARD_MIC_GAIN_DB);
    return ESP_OK;
}

esp_err_t board_audio_init(void)
{
    ESP_RETURN_ON_ERROR(init_i2c(), TAG, "i2c");
    ESP_RETURN_ON_ERROR(init_amp(), TAG, "amp");
    ESP_RETURN_ON_ERROR(init_i2s_duplex(), TAG, "i2s");
    ESP_RETURN_ON_ERROR(init_data_if(), TAG, "data_if");
    ESP_RETURN_ON_ERROR(init_es8311_output(), TAG, "es8311");
    ESP_RETURN_ON_ERROR(init_es7210_input(), TAG, "es7210");
    return ESP_OK;
}

esp_codec_dev_handle_t board_audio_get_output_dev(void) { return s_out_dev; }
esp_codec_dev_handle_t board_audio_get_input_dev(void)  { return s_in_dev; }

esp_err_t board_audio_read_mono(int16_t *dst, size_t frames)
{
    int16_t tmp[240 * BOARD_MIC_CHANNELS];
    size_t done = 0;
    while (done < frames) {
        size_t n = frames - done;
        if (n > 240) n = 240;
        esp_err_t err = esp_codec_dev_read(s_in_dev, tmp, n * BOARD_MIC_CHANNELS * sizeof(int16_t));
        if (err != ESP_OK) return err;
        for (size_t i = 0; i < n; i++) {
            dst[done + i] = tmp[i * BOARD_MIC_CHANNELS];   /* channel 0 */
        }
        done += n;
    }
    return ESP_OK;
}
