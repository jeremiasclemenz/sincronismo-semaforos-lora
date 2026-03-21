#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include <stdbool.h>

typedef enum {
    APP_DEVICE_MODE_SLAVE = 0,
    APP_DEVICE_MODE_MASTER = 1,
} app_device_mode_t;

typedef struct {
    app_device_mode_t device_mode;
} app_config_t;

static inline bool app_config_is_master(const app_config_t *config)
{
    return config != NULL && config->device_mode == APP_DEVICE_MODE_MASTER;
}

#endif