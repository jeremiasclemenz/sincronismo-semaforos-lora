#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_err.h"

#include "lora_sync.h"
#include "mec_sem_interface.h"
#include "web_dashboard.h"

static app_config_t app_config = {
    .device_mode = APP_DEVICE_MODE_SLAVE,
    // device_mode = APP_DEVICE_MODE_MASTER,  
};


void app_main(void)
{
    printf("Iniciando sistema de sincronismo de semáforos\n");

    // Inicialización global del sistema – solo deben llamarse UNA VEZ
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    BaseType_t task_ok = xTaskCreate(
        lora_sync_task,
        LORA_SYNC_TASK_NAME,
        LORA_SYNC_TASK_STACK_SIZE,
        &app_config,
        LORA_SYNC_TASK_PRIORITY,
        NULL
    );
    if (task_ok != pdPASS) {
        printf("[main] ERROR: no se pudo crear la tarea lora_sync_task\n");
    }

    task_ok = xTaskCreate(
        web_dashboard_task,
        WEB_DASHBOARD_TASK_NAME,
        WEB_DASHBOARD_TASK_STACK_SIZE,
        &app_config,
        WEB_DASHBOARD_TASK_PRIORITY,
        NULL
    );
    if (task_ok != pdPASS) {
        printf("[main] ERROR: no se pudo crear la tarea web_dashboard_task\n");
    }

    task_ok = xTaskCreate(
        mec_sem_interface_task,
        MEC_SEM_INTERFACE_TASK_NAME,
        MEC_SEM_INTERFACE_TASK_STACK_SIZE,
        NULL,
        MEC_SEM_INTERFACE_TASK_PRIORITY,
        NULL
    );
    if (task_ok != pdPASS) {
        printf("[main] ERROR: no se pudo crear la tarea mec_sem_interface_task\n");
    }
}