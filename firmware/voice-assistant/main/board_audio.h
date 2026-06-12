#pragma once
#include "esp_err.h"
#include "esp_codec_dev.h"
#include <stddef.h>

/* Brings up I2C, the speaker-amp (EXIO8), duplex I2S, the ES8311 output codec
   and the ES7210 input codec. Safe to call once at startup. */
esp_err_t board_audio_init(void);

/* The opened output codec device (ES8311), or NULL. */
esp_codec_dev_handle_t board_audio_get_output_dev(void);

/* The opened input codec device (ES7210), or NULL. */
esp_codec_dev_handle_t board_audio_get_input_dev(void);

/* Reads `frames` mono samples (channel 0 of the 4-ch ADC) into dst.
   Blocks until the I2S RX DMA has the data. Returns ESP_OK on success. */
esp_err_t board_audio_read_mono(int16_t *dst, size_t frames);
