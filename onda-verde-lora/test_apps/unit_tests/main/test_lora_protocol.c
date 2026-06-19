#include "unity.h"
#include "lora_protocol.h"

static lora_frame_t make_frame(uint8_t master_id, uint8_t seq, uint8_t cmd, uint16_t relay_ms)
{
    lora_frame_t f = {
        .master_id = master_id,
        .sequence  = seq,
        .command   = cmd,
        .relay_ms  = relay_ms,
    };
    f.checksum = lora_frame_checksum(&f);
    return f;
}

TEST_CASE("tamaño del frame es 6 bytes (packed)", "[lora_protocol]")
{
    TEST_ASSERT_EQUAL_UINT32(6, sizeof(lora_frame_t));
}

TEST_CASE("checksum es XOR de los primeros 5 bytes", "[lora_protocol]")
{
    lora_frame_t f = make_frame(0x01, 42, CMD_GREEN, 10000);
    const uint8_t *b = (const uint8_t *)&f;
    uint8_t expected = b[0] ^ b[1] ^ b[2] ^ b[3] ^ b[4];
    TEST_ASSERT_EQUAL_UINT8(expected, f.checksum);
}

TEST_CASE("checksum es determinista para el mismo contenido", "[lora_protocol]")
{
    lora_frame_t a = make_frame(0x01, 7, CMD_DEBUG, 0);
    lora_frame_t b = make_frame(0x01, 7, CMD_DEBUG, 0);
    TEST_ASSERT_EQUAL_UINT8(a.checksum, b.checksum);
}

TEST_CASE("frame válido con master_id esperado y checksum correcto", "[lora_protocol]")
{
    lora_frame_t f = make_frame(0x01, 1, CMD_GREEN, 5000);
    TEST_ASSERT_TRUE(lora_frame_valid(&f, 0x01));
}

TEST_CASE("frame inválido si master_id no coincide con el esperado", "[lora_protocol]")
{
    lora_frame_t f = make_frame(0x02, 1, CMD_GREEN, 5000);
    TEST_ASSERT_FALSE(lora_frame_valid(&f, 0x01));
}

TEST_CASE("frame inválido si el checksum tiene 1 bit corrupto", "[lora_protocol]")
{
    lora_frame_t f = make_frame(0x01, 1, CMD_GREEN, 5000);
    f.checksum ^= 0x01;
    TEST_ASSERT_FALSE(lora_frame_valid(&f, 0x01));
}

TEST_CASE("frame inválido si se altera el payload sin recalcular checksum", "[lora_protocol]")
{
    lora_frame_t f = make_frame(0x01, 1, CMD_GREEN, 5000);
    f.relay_ms = 9999; /* el checksum sigue siendo el de relay_ms=5000 */
    TEST_ASSERT_FALSE(lora_frame_valid(&f, 0x01));
}

TEST_CASE("rollover de sequence de 255 a 0 no rompe la validación", "[lora_protocol]")
{
    lora_frame_t f255 = make_frame(0x01, 255, CMD_GREEN, 1000);
    lora_frame_t f0   = make_frame(0x01, 0,   CMD_GREEN, 1000);
    TEST_ASSERT_TRUE(lora_frame_valid(&f255, 0x01));
    TEST_ASSERT_TRUE(lora_frame_valid(&f0, 0x01));
    TEST_ASSERT_NOT_EQUAL(f255.checksum, f0.checksum);
}

TEST_CASE("relay_ms se preserva little-endian en el struct packed", "[lora_protocol]")
{
    lora_frame_t f = make_frame(0x01, 1, CMD_GREEN, 0x1234);
    const uint8_t *b = (const uint8_t *)&f;
    TEST_ASSERT_EQUAL_UINT8(0x34, b[3]); /* LSB */
    TEST_ASSERT_EQUAL_UINT8(0x12, b[4]); /* MSB */
    TEST_ASSERT_EQUAL_UINT16(0x1234, f.relay_ms);
}
