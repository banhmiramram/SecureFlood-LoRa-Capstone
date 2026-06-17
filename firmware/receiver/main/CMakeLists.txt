idf_component_register(SRCS "main.c"
                       PRIV_REQUIRES spi_flash nvs_flash driver freertos lora_receive esp_timer parser lcd_display web_upload lora_crypto lora_replay
                       INCLUDE_DIRS "")
