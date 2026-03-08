#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "lora_sync.h"

// ---------------------------------------------------------
// Pin Definitions for ESP32-C3 Mini to LoRa Module
// ---------------------------------------------------------
#define LORA_SCK_PIN  4
#define LORA_MISO_PIN 5
#define LORA_MOSI_PIN 6
#define LORA_CS_PIN   7
#define LORA_RST_PIN  8
#define LORA_DIO0_PIN 10


// Este define servira para definir si el codigo es un MASTER o un SLAVE, dependiendo de esto se ejecutara el codigo correspondiente
// MASTER = 1, SLAVE = 0
#define MASTER_MODE 1

// Global handle for the SPI device
spi_device_handle_t lora_spi;

/**
 * @brief En esta configuración, se inicializa el bus SPI para comunicarse con el módulo LoRa, se configuran los pines necesarios (SCK, MISO, MOSI, CS, RST y DIO0) 
 */
void pinConfig(void)
{
        // 1. Configure the SPI bus
        spi_bus_config_t buscfg = {
            .miso_io_num = LORA_MISO_PIN,
            .mosi_io_num = LORA_MOSI_PIN,
            .sclk_io_num = LORA_SCK_PIN,
            .quadwp_io_num = -1,
            .quadhd_io_num = -1,
            .max_transfer_sz = 0
        };

        // Initialize the SPI bus (ESP32-C3 uses SPI2_HOST for general SPI)
        ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));

        // 2. Configure the SPI device (The LoRa module)
        spi_device_interface_config_t devcfg = {
            .clock_speed_hz = 1000000,
            .mode = 0,
            .spics_io_num = LORA_CS_PIN,
            .queue_size = 1
        };

        // Attach the LoRa module to the SPI bus
        ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &devcfg, &lora_spi));

        // 3. Configure the LoRa Reset (RST) pin as an output
        gpio_config_t rst_conf = {
            .intr_type = GPIO_INTR_DISABLE,
            .mode = GPIO_MODE_OUTPUT,
            .pin_bit_mask = (1ULL << LORA_RST_PIN),
            .pull_down_en = 0,
            .pull_up_en = 0
        };
        gpio_config(&rst_conf);

        // 4. Configure the DIO0 pin as an input with a rising edge interrupt
        gpio_config_t dio0_conf = {
            .intr_type = GPIO_INTR_POSEDGE, // Trigger interrupt on rising edge
            .mode = GPIO_MODE_INPUT,
            .pin_bit_mask = (1ULL << LORA_DIO0_PIN),
            .pull_down_en = 1,              // Pull down to avoid false triggers
            .pull_up_en = 0
        };
        gpio_config(&dio0_conf);

        // Turn the LoRa module on (Take it out of reset state)
        gpio_set_level(LORA_RST_PIN, 1);
        printf("[lora_sync] SPI bus and GPIO pins configured for LoRa module\n");
}






void lora_sync_task(void *pvParameters)
{
    (void)pvParameters;

    printf("[lora_sync] Initializing SPI for LoRa transceiver...\n");
    pinConfig(); // Inicializo SPI y GPIOs para el módulo LoRa
    printf("[lora_sync] SPI and GPIO initialization complete!\n");

    // ---------------------------------------------------------
    // Main RTOS Loop
    // ---------------------------------------------------------
    while (1) {
        printf("[lora_sync] Handling LoRa transceiver traffic and RTC synchronization\n");
        
        // Short delay to keep the task responsive and latency low
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}