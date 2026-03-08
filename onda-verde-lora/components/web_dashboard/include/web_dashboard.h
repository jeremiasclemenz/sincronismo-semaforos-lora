#ifndef WEB_DASHBOARD_H
#define WEB_DASHBOARD_H

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define WEB_DASHBOARD_TASK_NAME "web_dashboard_task"
#define WEB_DASHBOARD_TASK_STACK_SIZE 4096
#define WEB_DASHBOARD_TASK_PRIORITY 1

void web_dashboard_task(void *pvParameters);

#endif