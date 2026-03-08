#include "web_dashboard.h"

#include <stdio.h>

void web_dashboard_task(void *pvParameters)
{
    (void)pvParameters;

    while (1) {
        printf("[web_dashboard] Publicando datos en el dashboard web\n");
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}