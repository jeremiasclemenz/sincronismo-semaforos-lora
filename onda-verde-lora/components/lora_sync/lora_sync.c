#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "driver/gpio.h"
#include "lora_sync.h"
#include "lora.h"
#include "web_dashboard.h"

// ---------------------------------------------------------
// Logic I/O: Button (Master) and LED (Slave / Master local)
// ---------------------------------------------------------
#define BUTTON_PIN     2   // Input with internal pull-up (active LOW)
#define LED_PIN        3   // Output

// ---------------------------------------------------------
// MASTER = 1, SLAVE = 0
// ---------------------------------------------------------
#define MASTER_MODE  1

// ---------------------------------------------------------
// GPIO Initialization (button + LED only; SPI & LoRa pins
// are handled by the nopnop2002 lora component via Kconfig)
// ---------------------------------------------------------
static void gpio_init(void)
{
    // --- Button pin (input with pull-up, active LOW) ---
    gpio_config_t btn_conf = {
        .intr_type    = GPIO_INTR_DISABLE,
        .mode         = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << BUTTON_PIN),
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&btn_conf));

    // --- LED pin (output, initially off) ---
    gpio_config_t led_conf = {
        .intr_type    = GPIO_INTR_DISABLE,
        .mode         = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << LED_PIN),
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&led_conf));
    gpio_set_level(LED_PIN, 0);

    printf("[lora_sync] Button (GPIO %d) and LED (GPIO %d) configured\n", BUTTON_PIN, LED_PIN);
}

// =================================================================
// MASTER: mide duración de pulsación del botón y la envía por LoRa
// =================================================================
#if MASTER_MODE == 1

static void master_loop(void)
{
    bool       last_button  = true;   // pull-up → idle HIGH
    bool       led_active   = false;
    TickType_t press_tick   = 0;
    TickType_t led_on_tick  = 0;
    uint32_t   led_dur_ms   = 0;

    printf("[MASTER] Listo. Mantené pulsado el botón en GPIO %d...\n", BUTTON_PIN);

    while (1) {
        bool current_button = (bool)gpio_get_level(BUTTON_PIN);

        // --- Botón recién presionado (HIGH → LOW) ---
        if (last_button && !current_button) {
            press_tick = xTaskGetTickCount();
            snprintf(system_state, sizeof(system_state), "BUTTON PRESSED");
            printf("[MASTER] Botón presionado. Contando tiempo...\n");
            vTaskDelay(pdMS_TO_TICKS(50)); // anti-rebote
        }

        // --- Botón recién soltado (LOW → HIGH) ---
        if (!last_button && current_button) {
            uint32_t held_ms = (uint32_t)((xTaskGetTickCount() - press_tick) * portTICK_PERIOD_MS);

            printf("[MASTER] Sending beacon... duración: %lu ms\n", (unsigned long)held_ms);

            // Enviar como texto ASCII (igual que el Arduino original)
            char buf[12];
            snprintf(buf, sizeof(buf), "%lu", (unsigned long)held_ms);

            lora_send_packet((uint8_t *)buf, (int)strlen(buf));

            printf("[MASTER] Paquete enviado OK\n");

            // Actualizar variables del dashboard
            last_beacon_duration_ms = (int)held_ms;
            last_rssi = lora_packet_rssi();
            packet_counter++;
            snprintf(system_state, sizeof(system_state), "BEACON SENT");

            // Encender LED local por la misma duración
            if (held_ms > 0) {
                gpio_set_level(LED_PIN, 1);
                led_active  = true;
                led_on_tick = xTaskGetTickCount();
                led_dur_ms  = held_ms;
            }

            // Volver a modo recepción (por si se quiere escuchar ACKs a futuro)
            lora_receive();

            vTaskDelay(pdMS_TO_TICKS(50)); // anti-rebote
        }

        // --- Apagar LED local cuando venza el tiempo ---
        if (led_active) {
            uint32_t elapsed = (uint32_t)((xTaskGetTickCount() - led_on_tick) * portTICK_PERIOD_MS);
            if (elapsed >= led_dur_ms) {
                gpio_set_level(LED_PIN, 0);
                led_active = false;
                snprintf(system_state, sizeof(system_state), "IDLE");
            }
        }

        last_button = current_button;
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

#else
// =================================================================
// SLAVE: escucha paquetes LoRa y enciende el LED por la duración recibida
// =================================================================

static void slave_loop(void)
{
    bool       led_active   = false;
    TickType_t led_on_tick  = 0;
    uint32_t   led_dur_ms   = 0;

    printf("[SLAVE] Esperando paquetes LoRa...\n");
    lora_receive();

    while (1) {
        // Verificar si se recibió un paquete
        if (lora_received()) {
            uint8_t buf[16] = { 0 };
            int len = lora_receive_packet(buf, sizeof(buf) - 1);

            if (len > 0) {
                buf[len] = '\0';
                int rssi = lora_packet_rssi();
                printf("[SLAVE] Sync received – duración: '%s' ms  RSSI: %d\n", (char *)buf, rssi);

                uint32_t duration = (uint32_t)strtoul((char *)buf, NULL, 10);
                last_rssi = rssi;
                last_beacon_duration_ms = (int)duration;
                packet_counter++;
                if (duration > 0) {
                    snprintf(system_state, sizeof(system_state), "LED ON");
                    gpio_set_level(LED_PIN, 1);
                    led_active  = true;
                    led_on_tick = xTaskGetTickCount();
                    led_dur_ms  = duration;
                }
            }

            // Volver a modo recepción para el siguiente paquete
            lora_receive();
        }

        // --- Apagar LED cuando venza el tiempo ---
        if (led_active) {
            uint32_t elapsed = (uint32_t)((xTaskGetTickCount() - led_on_tick) * portTICK_PERIOD_MS);
            snprintf(system_state, sizeof(system_state), "LED OFF");
            if (elapsed >= led_dur_ms) {
                gpio_set_level(LED_PIN, 0);
                led_active = false;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

#endif // MASTER_MODE

// =================================================================
// FreeRTOS Task Entry Point
// =================================================================

void lora_sync_task(void *pvParameters)
{
    (void)pvParameters;

    printf("[lora_sync] Initializing GPIOs...\n");
    gpio_init();

    // lora_init() configura SPI y el módulo SX127x internamente
    // Los pines se configuran via menuconfig (idf.py menuconfig → LoRa Configuration)
    if (lora_init() == 0) {
        printf("[lora_sync] ERROR: No se pudo inicializar el módulo LoRa. Abortando tarea.\n");
        vTaskDelete(NULL);
        return;
    }

    lora_set_frequency(433E6);
    lora_enable_crc();
    printf("[lora_sync] Módulo LoRa inicializado correctamente (433 MHz)\n");

#if MASTER_MODE == 1
    printf("[lora_sync] Modo: MASTER\n");
    master_loop();
#else
    printf("[lora_sync] Modo: SLAVE\n");
    slave_loop();
#endif
}