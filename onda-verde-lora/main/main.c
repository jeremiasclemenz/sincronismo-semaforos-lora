#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "lora_sync.h"
#include "mec_sem_interface.h"
#include "web_dashboard.h"


// ---------------------------------------------------------
// MASTER = 1, SLAVE = 0
// ---------------------------------------------------------
#define MASTER_MODE  1


void app_main(void)
{
    printf("Iniciando sistema de sincronismo de semáforos\n");

    xTaskCreate(
        lora_sync_task,
        LORA_SYNC_TASK_NAME,
        LORA_SYNC_TASK_STACK_SIZE,
        NULL,
        LORA_SYNC_TASK_PRIORITY,
        NULL
    );

    xTaskCreate(
        web_dashboard_task,
        WEB_DASHBOARD_TASK_NAME,
        WEB_DASHBOARD_TASK_STACK_SIZE,
        NULL,
        WEB_DASHBOARD_TASK_PRIORITY,
        NULL
    );

    xTaskCreate(
        mec_sem_interface_task,
        MEC_SEM_INTERFACE_TASK_NAME,
        MEC_SEM_INTERFACE_TASK_STACK_SIZE,
        NULL,
        MEC_SEM_INTERFACE_TASK_PRIORITY,
        NULL
    );
}