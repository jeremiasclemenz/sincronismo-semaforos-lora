#ifndef EVENT_LOG_H
#define EVENT_LOG_H

#include <stdint.h>

/* ── Flags de evento ────────────────────────────────────────────────── */
#define EVT_TX_OK       0x01   /* Master: paquete enviado y ACK recibido */
#define EVT_TX_LOST     0x02   /* Master: sin ACK tras max_retries        */
#define EVT_RX_OK       0x04   /* Slave:  paquete recibido y válido       */
#define EVT_RX_ERR      0x08   /* Slave:  paquete corrupto o ID inválido  */
#define EVT_RELAY_ON    0x10   /* Slave:  relé activado                   */
#define EVT_DEBUG       0x20   /* Evento generado en modo DEBUG           */
#define EVT_BOOT        0x40   /* Arranque del sistema                    */

/* ── Entrada del log (8 bytes) ──────────────────────────────────────── */
typedef struct {
    uint32_t uptime_ms;   /* ms desde boot                    */
    int8_t   rssi;        /* dBm (-128..+127)                 */
    int8_t   snr_x4;      /* SNR × 4, resolución 0.25 dB     */
    uint8_t  sequence;    /* número de secuencia del paquete  */
    uint8_t  event;       /* flags de evento (EVT_*)          */
} log_entry_t;

/* ── Estadísticas calculadas ────────────────────────────────────────── */
typedef struct {
    int16_t  rssi_avg;         /* promedio dBm (solo entradas con RSSI válido) */
    int16_t  rssi_min;
    int16_t  rssi_max;
    uint16_t tx_ok_count;
    uint16_t tx_lost_count;
    uint16_t rx_ok_count;
    uint16_t relay_count;
    uint32_t avg_interval_ms;  /* tiempo promedio entre EVT_RX_OK consecutivos */
} log_stats_t;

/* ── API pública ────────────────────────────────────────────────────── */

/* Inicialización — llamar desde app_main antes de crear las tasks.
 * Crea el mutex interno y registra EVT_BOOT. */
void event_log_init(void);

/* Agregar una entrada al FIFO (thread-safe). */
void event_log_push(uint8_t event_flags, int8_t rssi, float snr, uint8_t seq);

/* Copiar hasta `max` entradas en `out` en orden cronológico.
 * Devuelve la cantidad copiada. */
uint16_t event_log_read_all(log_entry_t *out, uint16_t max);

/* Calcular estadísticas sobre todas las entradas del FIFO. */
void event_log_get_stats(log_stats_t *out);

/* Número de entradas actualmente en el FIFO. */
uint16_t event_log_count(void);

#endif /* EVENT_LOG_H */
