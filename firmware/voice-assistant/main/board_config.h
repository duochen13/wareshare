#pragma once
#include "driver/gpio.h"
#include "esp_io_expander.h"

/* I2C control bus (codec + IO expander share it) */
#define BOARD_I2C_PORT        0
#define BOARD_I2C_SDA_GPIO    GPIO_NUM_11
#define BOARD_I2C_SCL_GPIO    GPIO_NUM_10

/* I2S audio bus */
#define BOARD_I2S_MCLK_GPIO   GPIO_NUM_12
#define BOARD_I2S_BCLK_GPIO   GPIO_NUM_13
#define BOARD_I2S_WS_GPIO     GPIO_NUM_14
#define BOARD_I2S_DOUT_GPIO   GPIO_NUM_16   /* to ES8311 / speaker */
#define BOARD_I2S_DIN_GPIO    GPIO_NUM_15   /* from ES7210 / mics (unused in 1a) */

/* Speaker amplifier enable is NOT a GPIO: it is EXIO8 on the TCA9555 expander */
#define BOARD_AMP_EXIO_PIN    IO_EXPANDER_PIN_NUM_8

#define BOARD_AUDIO_SAMPLE_RATE   24000

/* ES7210 is a 4-channel ADC; we keep channel 0. */
#define BOARD_MIC_CHANNELS    4
#define BOARD_MIC_GAIN_DB     0.0f
