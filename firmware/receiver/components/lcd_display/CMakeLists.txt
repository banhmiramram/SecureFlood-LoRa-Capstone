idf_component_register(SRCS "lcd_display.c"
                       INCLUDE_DIRS "."
                       REQUIRES driver esp_driver_spi freertos)