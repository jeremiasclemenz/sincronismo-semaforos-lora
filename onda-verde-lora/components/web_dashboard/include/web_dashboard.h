#ifndef WEB_DASHBOARD_H
#define WEB_DASHBOARD_H

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define WEB_DASHBOARD_TASK_NAME "web_dashboard_task"
#define WEB_DASHBOARD_TASK_STACK_SIZE 8192
#define WEB_DASHBOARD_TASK_PRIORITY 1

// Variables compartidas entre lora_sync y web_dashboard
extern int  last_rssi;
extern int  packet_counter;
extern int  last_beacon_duration_ms;
extern char system_state[32];

void web_dashboard_task(void *pvParameters);

#endif