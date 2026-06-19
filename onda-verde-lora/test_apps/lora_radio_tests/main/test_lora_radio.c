/*
 * Tests on-target que requieren las 2 placas físicas (master + slave con
 * módulo SX127x) corriendo el MISMO binario. Cada placa entra al menú
 * interactivo de Unity al bootear (unity_run_menu()); para los casos
 * "[multi_device]" Unity pide elegir qué función correr en cada placa
 * (p.ej. "(1) radio_master_xxx" / "(2) radio_slave_xxx") — correr la opción
 * 1 en la placa MASTER y la opción 2 en la SLAVE, arrancando ambas dentro
 * de una ventana de ~10-15 s.
 *
 * IMPORTANTE: correr primero, en AMBAS placas, el test "00 - lora_init"
 * (una sola vez por sesión: lora_init() no es reentrante, llamarlo de nuevo
 * vuelve a inicializar el bus SPI y aborta).
 */
#include <string.h>
#include "unity.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "lora.h"
#include "lora_protocol.h"
#include "app_config.h"

/* Debe coincidir con RELAY_PIN definido en components/lora_sync/lora_sync.c */
#define TEST_RELAY_PIN 10

#define TEST_MASTER_ID          APP_CFG_MASTER_ID
#define TEST_EXPECTED_MASTER_ID APP_CFG_EXPECTED_MASTER_ID

static void send_frame(uint8_t master_id, uint8_t seq, uint8_t cmd, uint16_t relay_ms)
{
    lora_frame_t f = {
        .master_id = master_id,
        .sequence  = seq,
        .command   = cmd,
        .relay_ms  = relay_ms,
    };
    f.checksum = lora_frame_checksum(&f);
    lora_send_packet((uint8_t *)&f, sizeof(f));
}

/* Espera hasta timeout_ms un paquete LoRa de tamaño exacto lora_frame_t. */
static bool wait_for_frame(lora_frame_t *out, uint32_t timeout_ms)
{
    TickType_t t0 = xTaskGetTickCount();
    while ((xTaskGetTickCount() - t0) * portTICK_PERIOD_MS < timeout_ms) {
        if (lora_received()) {
            uint8_t buf[sizeof(lora_frame_t)];
            int len = lora_receive_packet(buf, sizeof(buf));
            if (len == (int)sizeof(lora_frame_t)) {
                memcpy(out, buf, sizeof(lora_frame_t));
                return true;
            }
            lora_receive(); /* re-armar RX si el paquete no tenía el tamaño esperado */
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    return false;
}

/* ── 00: inicialización del radio (correr una vez por sesión, en ambas placas) ── */
TEST_CASE("00 - lora_init con los mismos parámetros RF que main.c", "[radio][setup]")
{
    app_config_t cfg = APP_CONFIG_DEFAULT(APP_DEVICE_MODE_MASTER);

    TEST_ASSERT_EQUAL_MESSAGE(1, lora_init(),
        "lora_init() falló: revisar wiring SPI/RST del módulo SX127x");

    lora_set_frequency(cfg.lora_frequency);
    lora_set_spreading_factor(cfg.lora_sf);
    lora_set_bandwidth(cfg.lora_bw);
    lora_set_coding_rate(cfg.lora_cr);
    lora_set_tx_power(cfg.lora_tx_power);
    lora_enable_crc();

    printf("[radio_test] LoRa inicializado OK (SF%d BW_idx=%d CR=%d TX=%ddBm Freq=%ldHz)\n",
           cfg.lora_sf, cfg.lora_bw, cfg.lora_cr, cfg.lora_tx_power, cfg.lora_frequency);
}

/* ── 1: intercambio CMD_DEBUG con ACK (sin GPIO, automático en ambas puntas) ── */
static void radio_master_debug_ack(void)
{
    printf("[MASTER] Enviando CMD_DEBUG seq=77...\n");
    send_frame(TEST_MASTER_ID, 77, CMD_DEBUG, 0);
    lora_receive();

    lora_frame_t ack;
    bool got = wait_for_frame(&ack, 5000);
    TEST_ASSERT_TRUE_MESSAGE(got, "No se recibió ACK del slave en 5 s");
    TEST_ASSERT_EQUAL_UINT8(CMD_ACK, ack.command);
    TEST_ASSERT_EQUAL_UINT8(77, ack.sequence);
    TEST_ASSERT_EQUAL_UINT8(TEST_MASTER_ID, ack.master_id);
    TEST_ASSERT_EQUAL_UINT8(lora_frame_checksum(&ack), ack.checksum);

    int   rssi = lora_packet_rssi();
    float snr  = lora_packet_snr();
    printf("[MASTER] ACK OK  RSSI=%d  SNR=%.2f\n", rssi, snr);
    TEST_ASSERT_GREATER_OR_EQUAL(-130, rssi);
    TEST_ASSERT_LESS_OR_EQUAL(10, rssi);
}

static void radio_slave_debug_ack(void)
{
    printf("[SLAVE] Esperando CMD_DEBUG...\n");
    lora_receive();

    lora_frame_t f;
    bool got = wait_for_frame(&f, 10000);
    TEST_ASSERT_TRUE_MESSAGE(got, "No se recibió el frame del master en 10 s");
    TEST_ASSERT_TRUE(lora_frame_valid(&f, TEST_EXPECTED_MASTER_ID));
    TEST_ASSERT_EQUAL_UINT8(CMD_DEBUG, f.command);

    send_frame(TEST_EXPECTED_MASTER_ID, f.sequence, CMD_ACK, 0);
    lora_receive();
    printf("[SLAVE] ACK enviado (seq=%u)\n", f.sequence);
}

TEST_CASE_MULTIPLE_DEVICES("Intercambio CMD_DEBUG con ACK", "[radio][multi_device]",
                           radio_master_debug_ack, radio_slave_debug_ack);

/* ── 2: CMD_GREEN activa el relé del slave por relay_ms ── */
static void radio_master_green(void)
{
    printf("[MASTER] Enviando CMD_GREEN seq=78 relay_ms=2000...\n");
    send_frame(TEST_MASTER_ID, 78, CMD_GREEN, 2000);
    lora_receive();

    lora_frame_t ack;
    bool got = wait_for_frame(&ack, 5000);
    TEST_ASSERT_TRUE_MESSAGE(got, "No se recibió ACK del slave en 5 s");
    TEST_ASSERT_EQUAL_UINT8(CMD_ACK, ack.command);
    TEST_ASSERT_EQUAL_UINT8(78, ack.sequence);
    printf("[MASTER] ACK recibido, el slave debería activar su relé ahora\n");
}

static void radio_slave_green(void)
{
    gpio_config_t out_conf = {
        .intr_type    = GPIO_INTR_DISABLE,
        .mode         = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << TEST_RELAY_PIN),
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&out_conf));
    gpio_set_level(TEST_RELAY_PIN, 0);

    printf("[SLAVE] Esperando CMD_GREEN...\n");
    lora_receive();

    lora_frame_t f;
    bool got = wait_for_frame(&f, 10000);
    TEST_ASSERT_TRUE_MESSAGE(got, "No se recibió el frame del master en 10 s");
    TEST_ASSERT_TRUE(lora_frame_valid(&f, TEST_EXPECTED_MASTER_ID));
    TEST_ASSERT_EQUAL_UINT8(CMD_GREEN, f.command);

    /* Mismo orden que lora_sync.c: ACK primero, después se activa el relé. */
    send_frame(TEST_EXPECTED_MASTER_ID, f.sequence, CMD_ACK, 0);
    lora_receive();
    printf("[SLAVE] ACK enviado, activando GPIO%d por %u ms\n", TEST_RELAY_PIN, f.relay_ms);

    gpio_set_level(TEST_RELAY_PIN, 1);
    TickType_t on_tick = xTaskGetTickCount();
    vTaskDelay(pdMS_TO_TICKS(f.relay_ms));
    gpio_set_level(TEST_RELAY_PIN, 0);
    uint32_t elapsed_ms = (uint32_t)((xTaskGetTickCount() - on_tick) * portTICK_PERIOD_MS);

    printf("[SLAVE] Relé desactivado, estuvo ON %u ms\n", elapsed_ms);
    TEST_ASSERT_UINT32_WITHIN(50, f.relay_ms, elapsed_ms);
}

TEST_CASE_MULTIPLE_DEVICES("CMD_GREEN activa el relé del slave por relay_ms", "[radio][multi_device]",
                           radio_master_green, radio_slave_green);

/* ── 3: robustez — el slave rechaza un frame con master_id incorrecto ── */
static void radio_master_wrong_id(void)
{
    printf("[MASTER-ROGUE] Enviando frame con master_id=0x99 (incorrecto)...\n");
    send_frame(0x99 /* != TEST_EXPECTED_MASTER_ID */, 200, CMD_GREEN, 1000);
}

static void radio_slave_rejects_wrong_id(void)
{
    printf("[SLAVE] Esperando frame con master_id incorrecto...\n");
    lora_receive();

    lora_frame_t f;
    bool got = wait_for_frame(&f, 10000);
    TEST_ASSERT_TRUE_MESSAGE(got, "No se recibió ningún frame en 10 s (revisar alcance/timing)");
    TEST_ASSERT_FALSE_MESSAGE(lora_frame_valid(&f, TEST_EXPECTED_MASTER_ID),
        "El frame con master_id incorrecto fue aceptado como válido");
    printf("[SLAVE] Frame correctamente rechazado (master_id recibido=0x%02X)\n", f.master_id);
}

TEST_CASE_MULTIPLE_DEVICES("Slave rechaza frame con master_id incorrecto", "[radio][multi_device]",
                           radio_master_wrong_id, radio_slave_rejects_wrong_id);

/* ── 4: robustez — sin respuesta cuando el slave está apagado/fuera de alcance ──
 * Single-device: apagar o alejar físicamente el slave antes de correr este
 * test (solo en la placa MASTER). */
TEST_CASE("Master no recibe ACK con el slave apagado/fuera de alcance", "[radio][manual]")
{
    app_config_t cfg = APP_CONFIG_DEFAULT(APP_DEVICE_MODE_MASTER);
    uint32_t timeout_ms = cfg.ack_timeout_ms + 1000; /* margen sobre el timeout real */

    printf("[MASTER] Verificar que el slave está apagado/fuera de alcance antes de continuar.\n");
    send_frame(TEST_MASTER_ID, 250, CMD_DEBUG, 0);
    lora_receive();

    lora_frame_t ack;
    bool got = wait_for_frame(&ack, timeout_ms);
    TEST_ASSERT_FALSE_MESSAGE(got,
        "Se recibió una respuesta: el slave no estaba realmente apagado/fuera de alcance");
    printf("[MASTER] Confirmado: sin respuesta en %u ms (igual al ACK_TIMEOUT de la app real)\n",
           (unsigned)timeout_ms);
}
