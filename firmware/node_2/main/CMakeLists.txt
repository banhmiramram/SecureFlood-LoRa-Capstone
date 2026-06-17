idf_component_register(SRCS "main.c"
                       PRIV_REQUIRES spi_flash hc_sr04 esp_timer lora_as32_send nvs_flash driver freertos data_filter water_alarm lora_crypto ina219
                       INCLUDE_DIRS "")
