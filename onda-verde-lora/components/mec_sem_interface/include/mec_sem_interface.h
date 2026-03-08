#ifndef MEC_SEM_INTERFACE_H
#define MEC_SEM_INTERFACE_H

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define MEC_SEM_INTERFACE_TASK_NAME "mec_sem_interface_task"
#define MEC_SEM_INTERFACE_TASK_STACK_SIZE 3072
#define MEC_SEM_INTERFACE_TASK_PRIORITY 3

void mec_sem_interface_task(void *pvParameters);

#endif