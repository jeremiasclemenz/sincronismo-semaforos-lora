#ifndef WEB_DASHBOARD_H
#define WEB_DASHBOARD_H

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "app_config.h"

#define WEB_DASHBOARD_TASK_NAME       "web_dashboard_task"
#define WEB_DASHBOARD_TASK_STACK_SIZE  8192
#define WEB_DASHBOARD_TASK_PRIORITY    1

/* ── API de escritura (llamada desde lora_sync_task) ────────────────── */
void web_dashboard_set_rssi(int rssi);
void web_dashboard_set_state(const char *state);
void web_dashboard_inc_packets(void);
void web_dashboard_set_relay_active(bool active);

/* ── Tarea principal ────────────────────────────────────────────────── */
void web_dashboard_task(void *pvParameters);

#endif /* WEB_DASHBOARD_H */
