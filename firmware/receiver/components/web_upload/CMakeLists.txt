idf_component_register(
    SRCS "web_uploader.c" "http_hmac.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_wifi nvs_flash esp_http_client esp_event esp-tls esp_netif mbedtls 
)