idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES freertos lora_sync web_dashboard mec_sem_interface
)
