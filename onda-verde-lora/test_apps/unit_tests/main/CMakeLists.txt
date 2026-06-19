# lora_protocol.h vive en components/lora_sync/include pero es header-only
# (funciones static inline): se agrega su carpeta de include directamente para
# no arrastrar las dependencias pesadas del componente lora_sync (lora, web_dashboard).
idf_component_register(SRC_DIRS "."
                       INCLUDE_DIRS "." "../../../components/lora_sync/include"
                       PRIV_REQUIRES unity app_config event_log
                       WHOLE_ARCHIVE)
