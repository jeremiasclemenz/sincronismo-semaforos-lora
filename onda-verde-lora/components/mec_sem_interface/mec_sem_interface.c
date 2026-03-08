#include "mec_sem_interface.h"

#include <stdio.h>

void mec_sem_interface_task(void *pvParameters)
{
    (void)pvParameters;

    while (1) {
        printf("[mec_sem_interface] Sensando estado de semáforos y comunicando con el MEC\n");
        vTaskDelay(pdMS_TO_TICKS(250));
    }
}