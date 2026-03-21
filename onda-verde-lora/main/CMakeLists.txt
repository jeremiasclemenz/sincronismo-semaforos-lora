idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES freertos lora_sync web_dashboard mec_sem_interface app_config nvs_flash esp_netif esp_event
)
