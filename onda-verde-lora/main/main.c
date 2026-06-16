#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_err.h"

#include "app_config.h"
#include "event_log.h"
#include "lora_sync.h"
#include "mec_sem_interface.h"
#include "web_dashboard.h"

/* ── Configuración del dispositivo ──────────────────────────────────
 * Cambiar APP_DEVICE_MODE_MASTER → APP_DEVICE_MODE_SLAVE para el nodo Slave.
 * El resto de los parámetros usa los valores por defecto del §8.           */
static app_config_t app_config = APP_CONFIG_DEFAULT(APP_DEVICE_MODE_MASTER);

void app_main(void)
{
    printf("=== Sistema de sincronismo de semáforos — LoRa Sync ===\n");
    printf("Modo: %s\n",
           app_config_is_master(&app_config) ? "MASTER" : "SLAVE");

    /* ── Inicialización global (llamar UNA vez) ─────────────────── */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* ── Event log (antes de crear las tasks) ───────────────────── */
    event_log_init();

    /* ── Tasks ──────────────────────────────────────────────────── */
    BaseType_t ok;

    ok = xTaskCreate(lora_sync_task,
                     LORA_SYNC_TASK_NAME,
                     LORA_SYNC_TASK_STACK_SIZE,
                     &app_config,
                     LORA_SYNC_TASK_PRIORITY,
                     NULL);
    if (ok != pdPASS) {
        printf("[main] ERROR: no se pudo crear lora_sync_task\n");
    }

    ok = xTaskCreate(web_dashboard_task,
                     WEB_DASHBOARD_TASK_NAME,
                     WEB_DASHBOARD_TASK_STACK_SIZE,
                     &app_config,
                     WEB_DASHBOARD_TASK_PRIORITY,
                     NULL);
    if (ok != pdPASS) {
        printf("[main] ERROR: no se pudo crear web_dashboard_task\n");
    }

    ok = xTaskCreate(mec_sem_interface_task,
                     MEC_SEM_INTERFACE_TASK_NAME,
                     MEC_SEM_INTERFACE_TASK_STACK_SIZE,
                     NULL,
                     MEC_SEM_INTERFACE_TASK_PRIORITY,
                     NULL);
    if (ok != pdPASS) {
        printf("[main] ERROR: no se pudo crear mec_sem_interface_task\n");
    }
}
