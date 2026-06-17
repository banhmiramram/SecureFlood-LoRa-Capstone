idf_component_register(
    SRCS "lora_receive.c"
    INCLUDE_DIRS "."
    REQUIRES driver freertos nvs_flash esp_timer 
)