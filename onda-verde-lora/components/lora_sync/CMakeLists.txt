idf_component_register(SRCS "lora_sync.c"
                    INCLUDE_DIRS "include"
                    REQUIRES freertos driver esp_driver_gpio lora)