#include "event_log.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_timer.h"

/* ── FIFO circular ──────────────────────────────────────────────────── */
#define LOG_SIZE 1024

static log_entry_t       log_buf[LOG_SIZE];
static uint16_t          log_head  = 0;   /* próxima posición de escritura */
static uint16_t          log_count = 0;   /* entradas válidas (0..LOG_SIZE) */
static SemaphoreHandle_t log_mutex = NULL;

/* ── Helpers internos ───────────────────────────────────────────────── */

/* Índice de la entrada más antigua en el buffer circular. */
static inline uint16_t oldest_idx(void)
{
    if (log_count < LOG_SIZE) {
        return 0;
    }
    return log_head; /* cuando está lleno, head apunta a la más antigua */
}

/* ── API pública ────────────────────────────────────────────────────── */

void event_log_init(void)
{
    log_mutex = xSemaphoreCreateMutex();
    configASSERT(log_mutex != NULL);

    memset(log_buf, 0, sizeof(log_buf));
    log_head  = 0;
    log_count = 0;

    /* Registrar arranque del sistema */
    event_log_push(EVT_BOOT, 0, 0.0f, 0);
}

void event_log_push(uint8_t event_flags, int8_t rssi, float snr, uint8_t seq)
{
    log_entry_t e = {
        .uptime_ms = (uint32_t)(esp_timer_get_time() / 1000ULL),
        .rssi      = rssi,
        .snr_x4    = (int8_t)(snr * 4.0f),
        .sequence  = seq,
        .event     = event_flags,
    };

    xSemaphoreTake(log_mutex, portMAX_DELAY);
    log_buf[log_head] = e;
    log_head = (log_head + 1) % LOG_SIZE;
    if (log_count < LOG_SIZE) {
        log_count++;
    }
    xSemaphoreGive(log_mutex);
}

uint16_t event_log_read_all(log_entry_t *out, uint16_t max)
{
    if (out == NULL || max == 0) {
        return 0;
    }

    xSemaphoreTake(log_mutex, portMAX_DELAY);

    uint16_t n    = (log_count < max) ? log_count : max;
    uint16_t base = oldest_idx();

    for (uint16_t i = 0; i < n; i++) {
        out[i] = log_buf[(base + i) % LOG_SIZE];
    }

    xSemaphoreGive(log_mutex);
    return n;
}

void event_log_get_stats(log_stats_t *out)
{
    if (out == NULL) {
        return;
    }

    memset(out, 0, sizeof(log_stats_t));

    xSemaphoreTake(log_mutex, portMAX_DELAY);

    if (log_count == 0) {
        xSemaphoreGive(log_mutex);
        return;
    }

    uint16_t base = oldest_idx();

    int32_t  rssi_sum        = 0;
    int16_t  rssi_min        = 127;
    int16_t  rssi_max        = -128;
    uint16_t rssi_samples    = 0;

    uint32_t last_rx_ok_ms   = 0;
    bool     has_last_rx     = false;
    uint32_t interval_sum    = 0;
    uint16_t interval_count  = 0;

    for (uint16_t i = 0; i < log_count; i++) {
        const log_entry_t *e = &log_buf[(base + i) % LOG_SIZE];

        if (e->event & EVT_TX_OK)    out->tx_ok_count++;
        if (e->event & EVT_TX_LOST)  out->tx_lost_count++;
        if (e->event & EVT_RELAY_ON) out->relay_count++;

        if (e->event & EVT_RX_OK) {
            out->rx_ok_count++;

            /* Intervalo entre recepciones consecutivas */
            if (has_last_rx) {
                uint32_t dt = e->uptime_ms - last_rx_ok_ms;
                interval_sum += dt;
                interval_count++;
            }
            last_rx_ok_ms = e->uptime_ms;
            has_last_rx   = true;
        }

        /* RSSI relevante para TX_OK y RX_OK */
        if (e->event & (EVT_TX_OK | EVT_RX_OK)) {
            rssi_sum += e->rssi;
            if (e->rssi < rssi_min) rssi_min = e->rssi;
            if (e->rssi > rssi_max) rssi_max = e->rssi;
            rssi_samples++;
        }
    }

    xSemaphoreGive(log_mutex);

    if (rssi_samples > 0) {
        out->rssi_avg = (int16_t)(rssi_sum / rssi_samples);
        out->rssi_min = rssi_min;
        out->rssi_max = rssi_max;
    }

    if (interval_count > 0) {
        out->avg_interval_ms = interval_sum / interval_count;
    }
}

uint16_t event_log_count(void)
{
    xSemaphoreTake(log_mutex, portMAX_DELAY);
    uint16_t n = log_count;
    xSemaphoreGive(log_mutex);
    return n;
}
