#ifndef LORA_SYNC_H
#define LORA_SYNC_H

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define LORA_SYNC_TASK_NAME "lora_sync_task"
#define LORA_SYNC_TASK_STACK_SIZE 4096
#define LORA_SYNC_TASK_PRIORITY 5

void lora_sync_task(void *pvParameters);

#endif