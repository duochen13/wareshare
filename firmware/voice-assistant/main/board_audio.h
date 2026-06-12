#pragma once
#include "esp_err.h"
#include "esp_codec_dev.h"

/* Brings up I2C, the speaker-amp (EXIO8), I2S and the ES8311 output codec.
   Safe to call once at startup. */
esp_err_t board_audio_init(void);

/* Returns the opened output codec device, or NULL if init failed/not called. */
esp_codec_dev_handle_t board_audio_get_output_dev(void);
