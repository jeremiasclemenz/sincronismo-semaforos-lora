#ifndef LORA_PROTOCOL_H
#define LORA_PROTOCOL_H

#include <stdint.h>

/* ── Comandos ───────────────────────────────────────────────────────── */
#define CMD_GREEN  0x01   /* Master → Slave: activar relé             */
#define CMD_ACK    0x02   /* Slave  → Master: confirmación recepción  */
#define CMD_DEBUG  0x03   /* Master → Slave: paquete de prueba        */

/* ── Estructura del paquete (6 bytes, packed) ───────────────────────── */
typedef struct __attribute__((packed)) {
    uint8_t  master_id;   /* byte 0: ID del nodo master                       */
    uint8_t  sequence;    /* byte 1: Contador rolling 0–255 (detección duplic.)*/
    uint8_t  command;     /* byte 2: CMD_GREEN / CMD_ACK / CMD_DEBUG          */
    uint16_t relay_ms;    /* byte 3-4 (little-endian): Duración del relé en ms (little-endian)  */
    uint8_t  checksum;    /* byte 5: XOR de bytes 0..4                        */
} lora_frame_t;

/* Checksum de aplicación: XOR de los primeros 5 bytes del frame.
 * El CRC de hardware del SX127x protege la integridad RF; este
 * checksum verifica que el contenido sea del master esperado. */
static inline uint8_t lora_frame_checksum(const lora_frame_t *f)
{
    const uint8_t *b = (const uint8_t *)f;
    return b[0] ^ b[1] ^ b[2] ^ b[3] ^ b[4];
}

/* Validar un frame recibido contra el master_id esperado. */
static inline int lora_frame_valid(const lora_frame_t *f, uint8_t expected_master_id)
{
    return (f->master_id == expected_master_id) &&
           (lora_frame_checksum(f) == f->checksum);
}

#endif /* LORA_PROTOCOL_H */
