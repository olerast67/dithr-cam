idf_component_register(SRCS "app_main.c" "oled.c" "ui.c" "cam.c" "codec.c" "store.c" "input.c" "par.c" "diag.c"
                       INCLUDE_DIRS "."
                       REQUIRES fx jpegdec esp_driver_i2c esp_driver_gpio esp_driver_sdmmc fatfs sdmmc nvs_flash
                                esp_timer esp_psram)
target_compile_options(${COMPONENT_LIB} PRIVATE -O2)
