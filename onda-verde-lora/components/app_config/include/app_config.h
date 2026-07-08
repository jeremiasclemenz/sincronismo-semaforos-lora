#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    APP_DEVICE_MODE_SLAVE  = 0,
    APP_DEVICE_MODE_MASTER = 1,
} app_device_mode_t;

/* ── Valores por defecto ─────────────────────────────────────────────── */
#define APP_CFG_LORA_SF             9           /* Spreading Factor */
#define APP_CFG_LORA_BW             7           /* BW index: 7 = 125 kHz */
#define APP_CFG_LORA_CR             1           /* Coding Rate: 1 = 4/5 */
#define APP_CFG_LORA_TX_POWER       17          /* dBm */
#define APP_CFG_LORA_FREQUENCY      433000000L  /* Hz */

#define APP_CFG_LOCK_TIMEOUT_MS     20000UL     /* lock-out master post-envío */
#define APP_CFG_RELAY_DURATION_MS   10000UL     /* duración relé slave */
#define APP_CFG_SLAVE_LOCKOUT_MS    15000UL     /* lock-out slave post-relay */
#define APP_CFG_ACK_TIMEOUT_MS      1500UL      /* timeout espera ACK (incluye margen para
                                                    el lora_recover() del slave antes de ACKear) */
#define APP_CFG_MAX_RETRIES         3

#define APP_CFG_MASTER_ID           0x01        /* ID del nodo master */
#define APP_CFG_EXPECTED_MASTER_ID  0x01        /* ID que acepta el slave */
#define APP_CFG_DEBUG_INTERVAL_MS   120000UL    /* intervalo debug: 2 min */

/* ── Estructura de configuración ─────────────────────────────────────── */
typedef struct {
    app_device_mode_t device_mode;

    /* LoRa RF */
    uint8_t  lora_sf;           /* Spreading Factor (7–12) */
    uint8_t  lora_bw;           /* Bandwidth index 0–9 (7 = 125 kHz) */
    uint8_t  lora_cr;           /* Coding Rate 1–4 (1 = 4/5) */
    uint8_t  lora_tx_power;     /* TX Power dBm 2–17 */
    long     lora_frequency;    /* Frecuencia en Hz */

    /* Timing */
    uint32_t lock_timeout_ms;   /* lock-out master post-envío */
    uint32_t relay_duration_ms; /* duración activación relé slave */
    uint32_t slave_lockout_ms;  /* lock-out slave post-relay (evita duplicados) */
    uint32_t ack_timeout_ms;    /* timeout espera ACK */
    uint8_t  max_retries;       /* máximo reintentos sin ACK */

    /* IDs */
    uint8_t  master_id;         /* ID de este nodo (usado al enviar) */
    uint8_t  expected_master_id;/* ID del master que acepta el slave */

    /* Funcionalidades */
    bool     debug_mode;        /* modo debug activo */
    uint32_t debug_interval_ms; /* intervalo paquete CMD_DEBUG (modo automático) */
    bool     debug_manual_mode; /* true = envío manual desde dashboard; false = automático por intervalo */
} app_config_t;

/* ── Helpers ─────────────────────────────────────────────────────────── */
static inline bool app_config_is_master(const app_config_t *config)
{
    return config != NULL && config->device_mode == APP_DEVICE_MODE_MASTER;
}

/* Inicializador con todos los valores por defecto (C99 designated initializers).
 * Uso: static app_config_t cfg = APP_CONFIG_DEFAULT(APP_DEVICE_MODE_MASTER); */
#define APP_CONFIG_DEFAULT(_mode) {                             \
    .device_mode        = (_mode),                             \
    .lora_sf            = APP_CFG_LORA_SF,                     \
    .lora_bw            = APP_CFG_LORA_BW,                     \
    .lora_cr            = APP_CFG_LORA_CR,                     \
    .lora_tx_power      = APP_CFG_LORA_TX_POWER,               \
    .lora_frequency     = APP_CFG_LORA_FREQUENCY,              \
    .lock_timeout_ms    = APP_CFG_LOCK_TIMEOUT_MS,             \
    .relay_duration_ms  = APP_CFG_RELAY_DURATION_MS,           \
    .slave_lockout_ms   = APP_CFG_SLAVE_LOCKOUT_MS,            \
    .ack_timeout_ms     = APP_CFG_ACK_TIMEOUT_MS,              \
    .max_retries        = APP_CFG_MAX_RETRIES,                 \
    .master_id          = APP_CFG_MASTER_ID,                   \
    .expected_master_id = APP_CFG_EXPECTED_MASTER_ID,          \
    .debug_mode         = false,                               \
    .debug_interval_ms  = APP_CFG_DEBUG_INTERVAL_MS,           \
    .debug_manual_mode  = false,                               \
}

#endif /* APP_CONFIG_H */
