#include "mec_sem_interface.h"

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* El GPIO del relé (pin 10) lo inicializa lora_sync_task según el modo
 * del dispositivo. Esta tarea no toca el pin para evitar conflictos. */

void mec_sem_interface_task(void *pvParameters)
{
    (void)pvParameters;

    printf("[mec_sem] Tarea iniciada\n");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
