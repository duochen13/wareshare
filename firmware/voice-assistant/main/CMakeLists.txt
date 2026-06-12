idf_component_register(
    SRCS "main.c" "board_audio.c"
    INCLUDE_DIRS "."
    REQUIRES esp_driver_gpio esp_driver_i2c esp_driver_i2s esp_codec_dev esp_io_expander_tca95xx_16bit
)
