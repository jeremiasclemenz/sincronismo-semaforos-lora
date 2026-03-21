idf_component_register(
    SRCS "web_dashboard.c"
    INCLUDE_DIRS "include"
    REQUIRES freertos esp_wifi esp_netif esp_event esp_http_server nvs_flash app_config
)