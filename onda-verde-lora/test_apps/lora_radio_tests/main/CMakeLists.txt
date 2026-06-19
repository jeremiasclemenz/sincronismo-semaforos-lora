# lora_protocol.h vive en components/lora_sync/include pero es header-only
# (funciones static inline): se agrega su carpeta de include directamente para
# no arrastrar las dependencias pesadas del componente lora_sync (web_dashboard).
idf_component_register(SRC_DIRS "."
                       INCLUDE_DIRS "." "../../../components/lora_sync/include"
                       PRIV_REQUIRES unity lora app_config driver esp_driver_gpio
                       WHOLE_ARCHIVE)
