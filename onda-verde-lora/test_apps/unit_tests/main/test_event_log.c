#include "unity.h"
#include "event_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* Unity llama setUp() antes de cada TEST_CASE de este archivo (símbolo
 * débil definido por el componente unity; lo sobreescribimos aquí).
 * event_log_init() resetea el FIFO y registra un EVT_BOOT inicial. Crea un
 * mutex nuevo en cada llamada: aceptable en este test app de vida corta,
 * no es código de producción. */
void setUp(void)
{
    event_log_init();
}

TEST_CASE("event_log_init deja el FIFO con un único EVT_BOOT", "[event_log]")
{
    TEST_ASSERT_EQUAL_UINT16(1, event_log_count());

    log_entry_t out;
    TEST_ASSERT_EQUAL_UINT16(1, event_log_read_all(&out, 1));
    TEST_ASSERT_EQUAL_UINT8(EVT_BOOT, out.event);
}

TEST_CASE("event_log_push + event_log_read_all preservan el orden cronológico", "[event_log]")
{
    event_log_push(EVT_TX_OK, -50, 1.0f, 10);
    event_log_push(EVT_RX_OK, -60, 2.0f, 11);
    event_log_push(EVT_TX_LOST, 0, 0.0f, 12);

    log_entry_t out[8];
    uint16_t n = event_log_read_all(out, 8);

    /* setUp() ya registró 1 entrada EVT_BOOT antes de estas 3 */
    TEST_ASSERT_EQUAL_UINT16(4, n);
    TEST_ASSERT_EQUAL_UINT8(EVT_BOOT, out[0].event);
    TEST_ASSERT_EQUAL_UINT8(EVT_TX_OK, out[1].event);
    TEST_ASSERT_EQUAL_UINT8(10, out[1].sequence);
    TEST_ASSERT_EQUAL_UINT8(EVT_RX_OK, out[2].event);
    TEST_ASSERT_EQUAL_UINT8(11, out[2].sequence);
    TEST_ASSERT_EQUAL_UINT8(EVT_TX_LOST, out[3].event);
    TEST_ASSERT_EQUAL_UINT8(12, out[3].sequence);
}

TEST_CASE("event_log_count refleja la cantidad de entradas almacenadas", "[event_log]")
{
    TEST_ASSERT_EQUAL_UINT16(1, event_log_count()); /* solo EVT_BOOT tras setUp() */
    event_log_push(EVT_DEBUG, 0, 0.0f, 1);
    event_log_push(EVT_DEBUG, 0, 0.0f, 2);
    TEST_ASSERT_EQUAL_UINT16(3, event_log_count());
}

TEST_CASE("el buffer circular descarta la entrada más vieja al superar LOG_SIZE", "[event_log]")
{
    const uint16_t LOG_SIZE = 1024; /* debe coincidir con LOG_SIZE en event_log.c */

    /* setUp() ya pushea 1 EVT_BOOT; estos LOG_SIZE pushes lo desplazan fuera del FIFO */
    for (uint16_t i = 0; i < LOG_SIZE; i++) {
        event_log_push(EVT_TX_OK, (int8_t)(i % 100), 0.0f, (uint8_t)i);
    }

    TEST_ASSERT_EQUAL_UINT16(LOG_SIZE, event_log_count());

    log_entry_t oldest;
    TEST_ASSERT_EQUAL_UINT16(1, event_log_read_all(&oldest, 1));
    TEST_ASSERT_FALSE(oldest.event & EVT_BOOT);
    TEST_ASSERT_TRUE(oldest.event & EVT_TX_OK);
    TEST_ASSERT_EQUAL_UINT8(0, oldest.sequence);
}

TEST_CASE("event_log_get_stats calcula contadores y RSSI correctamente", "[event_log]")
{
    event_log_push(EVT_TX_OK, -40, 0.0f, 1);
    event_log_push(EVT_TX_OK, -80, 0.0f, 2);
    event_log_push(EVT_TX_LOST, 0, 0.0f, 3);
    event_log_push(EVT_RELAY_ON, 0, 0.0f, 4);
    event_log_push(EVT_RX_OK, -60, 0.0f, 5);
    event_log_push(EVT_RX_OK, -60, 0.0f, 6);

    log_stats_t stats;
    event_log_get_stats(&stats);

    TEST_ASSERT_EQUAL_UINT16(2, stats.tx_ok_count);
    TEST_ASSERT_EQUAL_UINT16(1, stats.tx_lost_count);
    TEST_ASSERT_EQUAL_UINT16(2, stats.rx_ok_count);
    TEST_ASSERT_EQUAL_UINT16(1, stats.relay_count);

    /* RSSI considera EVT_TX_OK + EVT_RX_OK: -40, -80, -60, -60 -> avg=-60 */
    TEST_ASSERT_EQUAL_INT16(-80, stats.rssi_min);
    TEST_ASSERT_EQUAL_INT16(-40, stats.rssi_max);
    TEST_ASSERT_EQUAL_INT16(-60, stats.rssi_avg);
}

TEST_CASE("event_log_get_stats calcula avg_interval_ms entre EVT_RX_OK consecutivos", "[event_log]")
{
    event_log_push(EVT_RX_OK, -50, 0.0f, 1);
    vTaskDelay(pdMS_TO_TICKS(50));
    event_log_push(EVT_RX_OK, -50, 0.0f, 2);
    vTaskDelay(pdMS_TO_TICKS(80));
    event_log_push(EVT_RX_OK, -50, 0.0f, 3);

    log_stats_t stats;
    event_log_get_stats(&stats);

    /* dos intervalos ~50ms y ~80ms -> promedio ~65ms (tolerancia por scheduler) */
    TEST_ASSERT_UINT32_WITHIN(20, 65, stats.avg_interval_ms);
}

TEST_CASE("event_log_get_stats con FIFO vacío de eventos relevantes no rompe nada", "[event_log]")
{
    /* solo el EVT_BOOT de setUp(), sin TX/RX/RELAY */
    log_stats_t stats;
    event_log_get_stats(&stats);

    TEST_ASSERT_EQUAL_UINT16(0, stats.tx_ok_count);
    TEST_ASSERT_EQUAL_UINT16(0, stats.rx_ok_count);
    TEST_ASSERT_EQUAL_UINT16(0, stats.relay_count);
    TEST_ASSERT_EQUAL_UINT32(0, stats.avg_interval_ms);
}
