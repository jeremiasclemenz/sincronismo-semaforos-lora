#include "lora_sync.h"
#include "lora_protocol.h"
#include "event_log.h"
#include "web_dashboard.h"

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "sdkconfig.h"
#include "lora.h"

/* ── Pines de aplicación ────────────────────────────────────────────── */
/* INPUT_220VAC_PIN: optoacoplador 220VAC → GPIO master (activo LOW, pull-up)
 * RELAY_PIN:        salida relé slave (activo HIGH, NA)                     */
#define INPUT_220VAC_PIN    2   /* Master: optoacoplador 220VAC (pull-down externo, activo HIGH) */
#define RELAY_PIN          10   /* Slave:  salida relé NA (activo HIGH)                         */

/* ── Helpers de tiempo ──────────────────────────────────────────────── */
static inline uint32_t ms_elapsed(TickType_t since)
{
    return (uint32_t)((xTaskGetTickCount() - since) * portTICK_PERIOD_MS);
}

/* ── Envío de frame LoRa ────────────────────────────────────────────── */
static void send_lora_frame(const app_config_t *cfg,
                             uint8_t command,
                             uint8_t sequence,
                             uint16_t relay_ms)
{
    lora_frame_t f = {
        .master_id = cfg->master_id,
        .sequence  = sequence,
        .command   = command,
        .relay_ms  = relay_ms,
    };
    f.checksum = lora_frame_checksum(&f);
    lora_send_packet((uint8_t *)&f, sizeof(lora_frame_t));
}

/* ── Recepción y validación de ACK ─────────────────────────────────── */
/* Devuelve true si se recibió un ACK válido con la sequence esperada. */
static bool try_receive_ack(const app_config_t *cfg, uint8_t expected_seq)
{
    if (!lora_received()) {
        return false;
    }
    uint8_t buf[sizeof(lora_frame_t)];
    int len = lora_receive_packet(buf, sizeof(buf));
    if (len != sizeof(lora_frame_t)) {
        return false;
    }
    lora_frame_t *f = (lora_frame_t *)buf;
    return (f->command   == CMD_ACK)         &&
           (f->master_id == cfg->master_id)  &&
           (f->sequence  == expected_seq)    &&
           (lora_frame_checksum(f) == f->checksum);
}

/* ═══════════════════════════════════════════════════════════════════════
 * MASTER — state machine
 * ═══════════════════════════════════════════════════════════════════════ */
typedef enum {
    MASTER_IDLE,
    MASTER_SEND,
    MASTER_WAIT_ACK,
    MASTER_RETRY,
    MASTER_LOCK,
} master_state_t;

static void master_loop(const app_config_t *cfg)
{
    master_state_t state     = MASTER_IDLE;
    uint8_t        retries   = 0;
    uint8_t        sequence  = 0;                       // Cuenta en que parte de la state machine estamos
    TickType_t     lock_tick = 0;
    TickType_t     ack_tick  = 0;
    TickType_t     dbg_tick  = 0;
    TickType_t     relay_tick = 0;     /* inicio del relé inferido en el slave */
    bool           relay_inf  = false; /* relé del slave inferido activo       */
    bool           sent_green = false; /* último frame enviado fue CMD_GREEN   */
    bool           sent_debug_manual = false; /* último frame fue CMD_DEBUG manual (sin LOCK) */
    bool           last_gpio = false; /* pull-down externo → idle LOW */

    printf("[MASTER] Listo. Esperando señal en GPIO %d...\n", INPUT_220VAC_PIN);

    while (1) {
        bool gpio_now = (bool)gpio_get_level(INPUT_220VAC_PIN);

        /* Apagar el relé inferido cuando venza relay_duration_ms.
         * El master no recibe confirmación del apagado: lo infiere a partir
         * de la duración que él mismo envió en el frame CMD_GREEN. */
        if (relay_inf && ms_elapsed(relay_tick) >= cfg->relay_duration_ms) {
            relay_inf = false;
            web_dashboard_set_relay_active(false);
        }

        switch (state) {

        case MASTER_IDLE: {
            /* Modo DEBUG: enviar CMD_DEBUG periódicamente (automático)
             * o solo cuando el usuario lo dispara desde el dashboard (manual). */
            if (cfg->debug_mode) {
                bool send_debug = cfg->debug_manual_mode
                    ? web_dashboard_consume_debug_trigger()
                    : (ms_elapsed(dbg_tick) >= cfg->debug_interval_ms);
                if (send_debug) {
                    sequence++;
                    printf("[MASTER][DEBUG] Enviando CMD_DEBUG seq=%u\n", sequence);
                    sent_green        = false;
                    sent_debug_manual = cfg->debug_manual_mode;
                    send_lora_frame(cfg, CMD_DEBUG, sequence, 0);
                    lora_receive();
                    ack_tick  = xTaskGetTickCount();
                    dbg_tick  = xTaskGetTickCount();
                    retries   = 0;
                    state     = MASTER_WAIT_ACK;
                    event_log_push(EVT_DEBUG, 0, 0.0f, sequence);
                    web_dashboard_set_state("DEBUG SENT");
                    break;
                }
            }

            /* Ignorar si estamos en lock-out */
            bool locked = (lock_tick != 0) &&
                          (ms_elapsed(lock_tick) < cfg->lock_timeout_ms);
            if (locked) break;

            /* Flanco de subida detectado (LOW → HIGH con pull-down externo) */
            if (!last_gpio && gpio_now) {
                retries = 0;
                state   = MASTER_SEND;
            }
            break;
        }

        case MASTER_SEND:
            sequence++;
            printf("[MASTER] Enviando CMD_GREEN seq=%u\n", sequence);
            sent_green        = true;
            sent_debug_manual = false;
            send_lora_frame(cfg, CMD_GREEN, sequence, (uint16_t)cfg->relay_duration_ms);
            lora_receive();
            ack_tick = xTaskGetTickCount();
            state    = MASTER_WAIT_ACK;
            web_dashboard_set_state("BEACON SENT");
            web_dashboard_inc_packets();
            break;

        case MASTER_WAIT_ACK:
            if (try_receive_ack(cfg, sequence)) {
                int   rssi = lora_packet_rssi();
                float snr  = lora_packet_snr();
                printf("[MASTER] ACK recibido OK  seq=%u  RSSI=%d  SNR=%.1f\n",
                       sequence, rssi, snr);
                event_log_push(EVT_TX_OK, (int8_t)rssi, snr, sequence);
                web_dashboard_set_rssi(rssi);
                /* ACK a un CMD_GREEN ⇒ el slave activó su relé: reflejarlo
                 * en el dashboard del master durante relay_duration_ms.
                 * (CMD_DEBUG también se ACKea pero no activa relé.) */
                if (sent_green) {
                    relay_inf  = true;
                    relay_tick = xTaskGetTickCount();
                    web_dashboard_set_relay_active(true);
                }
                if (sent_debug_manual) {
                    /* Debug manual: sin LOCK, listo para el próximo envío inmediato. */
                    web_dashboard_set_state("IDLE");
                    state = MASTER_IDLE;
                } else {
                    web_dashboard_set_state("LOCK");
                    lock_tick = xTaskGetTickCount();
                    state     = MASTER_LOCK;
                }
            } else if (ms_elapsed(ack_tick) >= cfg->ack_timeout_ms) {
                retries++;
                printf("[MASTER] Timeout ACK  seq=%u  intento=%u/%u\n",
                       sequence, retries, cfg->max_retries);
                if (retries < cfg->max_retries) {
                    state = MASTER_RETRY;
                } else {
                    printf("[MASTER] Sin ACK tras %u intentos — EVT_TX_LOST\n",
                           cfg->max_retries);
                    event_log_push(EVT_TX_LOST, 0, 0.0f, sequence);
                    if (sent_debug_manual) {
                        web_dashboard_set_state("IDLE");
                        state = MASTER_IDLE;
                    } else {
                        web_dashboard_set_state("LOCK");
                        lock_tick = xTaskGetTickCount();
                        state     = MASTER_LOCK;
                    }
                }
            }
            break;

        case MASTER_RETRY:
            printf("[MASTER] Reintento %u/%u  seq=%u\n",
                   retries, cfg->max_retries, sequence);
            if (sent_green) {
                send_lora_frame(cfg, CMD_GREEN, sequence, (uint16_t)cfg->relay_duration_ms);
            } else {
                send_lora_frame(cfg, CMD_DEBUG, sequence, 0);
            }
            lora_receive();
            ack_tick = xTaskGetTickCount();
            state    = MASTER_WAIT_ACK;
            break;

        case MASTER_LOCK: {
            bool still_locked = (ms_elapsed(lock_tick) < cfg->lock_timeout_ms); // lock tick tiene que ser mayor a lock timeout para salir del lock
            if (!still_locked) {
                printf("[MASTER] Lock-out vencido. Volviendo a IDLE.\n");
                state = MASTER_IDLE;
                web_dashboard_set_state("IDLE");
            }
            break;
        }
        } /* switch */

        last_gpio = gpio_now;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

/* ═══════════════════════════════════════════════════════════════════════
 * SLAVE — state machine
 * ═══════════════════════════════════════════════════════════════════════ */
typedef enum {
    SLAVE_LISTEN,
    SLAVE_RELAY_ON,
    SLAVE_RELAY_OFF,
} slave_state_t;

static void slave_loop(const app_config_t *cfg)
{
    slave_state_t state        = SLAVE_LISTEN;
    TickType_t    relay_tick   = 0;
    TickType_t    lockout_tick = 0;
    bool          in_lockout   = false;

    printf("[SLAVE] Esperando paquetes LoRa...\n");
    lora_receive();

    while (1) {
        switch (state) {

        case SLAVE_LISTEN: {
            if (!lora_received()) break;

            uint8_t buf[sizeof(lora_frame_t)];
            int len = lora_receive_packet(buf, sizeof(buf));

            if (len != (int)sizeof(lora_frame_t)) {
                lora_receive();
                break;
            }


            // Recibi el paquete, ahora lo valido y proceso
            
            lora_frame_t *f   = (lora_frame_t *)buf;
            int           rssi = lora_packet_rssi();
            float         snr  = lora_packet_snr();

            /* Validar master_id y checksum */
            if (!lora_frame_valid(f, cfg->expected_master_id)) {
                printf("[SLAVE] Paquete inválido  master_id=0x%02X  seq=%u\n",
                       f->master_id, f->sequence);
                event_log_push(EVT_RX_ERR, (int8_t)rssi, snr, f->sequence);
                lora_receive();
                break;
            }

            printf("[SLAVE] Paquete OK  cmd=0x%02X  seq=%u  RSSI=%d  SNR=%.1f\n",
                   f->command, f->sequence, rssi, snr);

            /* Enviar ACK */
            lora_frame_t ack = {
                .master_id = cfg->master_id,
                .sequence  = f->sequence,
                .command   = CMD_ACK,
                .relay_ms  = 0,
            };
            ack.checksum = lora_frame_checksum(&ack);
            lora_send_packet((uint8_t *)&ack, sizeof(lora_frame_t));
            lora_receive();

            /* Registrar recepción */
            uint8_t evt_flags = EVT_RX_OK;
            if (f->command == CMD_DEBUG) evt_flags |= EVT_DEBUG;
            event_log_push(evt_flags, (int8_t)rssi, snr, f->sequence);
            web_dashboard_set_rssi(rssi);
            web_dashboard_inc_packets();

            /* Activar relé solo si CMD_GREEN y no está en lock-out */
            if (f->command == CMD_GREEN && !in_lockout) {
                gpio_set_level(RELAY_PIN, 1);
                relay_tick = xTaskGetTickCount();
                event_log_push(EVT_RELAY_ON, 0, 0.0f, f->sequence);
                web_dashboard_set_state("RELAY ON");
                web_dashboard_set_relay_active(true);
                printf("[SLAVE] Relé activado por %lu ms\n",
                       (unsigned long)cfg->relay_duration_ms);
                state = SLAVE_RELAY_ON;
            } else if (f->command == CMD_GREEN && in_lockout) {
                printf("[SLAVE] CMD_GREEN ignorado — slave en lock-out\n");
                web_dashboard_set_state("LISTEN");
            } else {
                /* CMD_DEBUG u otro comando: sin relé */
                web_dashboard_set_state("LISTEN");
            }
            break;
        }

        case SLAVE_RELAY_ON:
            if (ms_elapsed(relay_tick) >= cfg->relay_duration_ms) {
                gpio_set_level(RELAY_PIN, 0);
                web_dashboard_set_relay_active(false);
                lockout_tick = xTaskGetTickCount();
                in_lockout   = true;
                printf("[SLAVE] Relé desactivado. Lock-out %lu ms\n",
                       (unsigned long)cfg->slave_lockout_ms);
                web_dashboard_set_state("LISTEN");
                state = SLAVE_RELAY_OFF;
            }
            break;

        case SLAVE_RELAY_OFF:
            if (ms_elapsed(lockout_tick) >= cfg->slave_lockout_ms) {
                in_lockout = false;
                printf("[SLAVE] Lock-out vencido. Escuchando.\n");
                state = SLAVE_LISTEN;
            }
            break;

        } /* switch */

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

/* ═══════════════════════════════════════════════════════════════════════
 * Inicialización de pines de aplicación
 * ═══════════════════════════════════════════════════════════════════════ */
static void app_gpio_init(bool is_master)
{
    if (is_master) {
        gpio_config_t in_conf = {
            .intr_type    = GPIO_INTR_DISABLE,
            .mode         = GPIO_MODE_INPUT,
            .pin_bit_mask = (1ULL << INPUT_220VAC_PIN),
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .pull_up_en   = GPIO_PULLUP_DISABLE,
        };
        ESP_ERROR_CHECK(gpio_config(&in_conf));
        printf("[lora_sync] GPIO %d configurado como entrada 220VAC\n",
               INPUT_220VAC_PIN);
    } else {
        gpio_config_t out_conf = {
            .intr_type    = GPIO_INTR_DISABLE,
            .mode         = GPIO_MODE_OUTPUT,
            .pin_bit_mask = (1ULL << RELAY_PIN),
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .pull_up_en   = GPIO_PULLUP_DISABLE,
        };
        ESP_ERROR_CHECK(gpio_config(&out_conf));
        gpio_set_level(RELAY_PIN, 0); /* relé abierto al arrancar */
        printf("[lora_sync] GPIO %d configurado como salida relé\n", RELAY_PIN);
    }
}

/* ═══════════════════════════════════════════════════════════════════════
 * Punto de entrada de la tarea FreeRTOS
 * ═══════════════════════════════════════════════════════════════════════ */
void lora_sync_task(void *pvParameters)
{
    const app_config_t *cfg = (const app_config_t *)pvParameters;
    bool is_master = app_config_is_master(cfg);

    /* Configurar pines de la aplicación */
    app_gpio_init(is_master);

    /* Inicializar módulo LoRa con parámetros de app_config */
    if (lora_init() == 0) {
        printf("[lora_sync] ERROR: No se pudo inicializar el módulo LoRa.\n");
        vTaskDelete(NULL);
        return;
    }

    lora_set_frequency(cfg->lora_frequency);
    lora_set_spreading_factor(cfg->lora_sf);
    lora_set_bandwidth(cfg->lora_bw);
    lora_set_coding_rate(cfg->lora_cr);
    lora_set_tx_power(cfg->lora_tx_power);
    lora_enable_crc();

    printf("[lora_sync] LoRa OK — SF%d BW_idx=%d CR=%d TX=%ddBm Freq=%ldHz\n",
           cfg->lora_sf, cfg->lora_bw, cfg->lora_cr,
           cfg->lora_tx_power, cfg->lora_frequency);

    if (is_master) {
        printf("[lora_sync] Modo: MASTER  id=0x%02X\n", cfg->master_id);
        master_loop(cfg);
    } else {
        printf("[lora_sync] Modo: SLAVE  esperando master_id=0x%02X\n",
               cfg->expected_master_id);
        slave_loop(cfg);
    }
}
