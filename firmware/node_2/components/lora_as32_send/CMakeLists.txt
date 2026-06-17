idf_component_register(
    SRCS "lora_as32_send.c"
    INCLUDE_DIRS "."
    REQUIRES spi_flash nvs_flash driver freertos
)