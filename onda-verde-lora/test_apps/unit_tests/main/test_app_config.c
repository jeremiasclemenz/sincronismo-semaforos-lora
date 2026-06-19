#include "unity.h"
#include "app_config.h"

TEST_CASE("APP_CONFIG_DEFAULT(MASTER) usa los valores APP_CFG_* esperados", "[app_config]")
{
    app_config_t cfg = APP_CONFIG_DEFAULT(APP_DEVICE_MODE_MASTER);

    TEST_ASSERT_EQUAL(APP_DEVICE_MODE_MASTER, cfg.device_mode);
    TEST_ASSERT_EQUAL_UINT8(APP_CFG_LORA_SF, cfg.lora_sf);
    TEST_ASSERT_EQUAL_UINT8(APP_CFG_LORA_BW, cfg.lora_bw);
    TEST_ASSERT_EQUAL_UINT8(APP_CFG_LORA_CR, cfg.lora_cr);
    TEST_ASSERT_EQUAL_UINT8(APP_CFG_LORA_TX_POWER, cfg.lora_tx_power);
    TEST_ASSERT_EQUAL_INT32(APP_CFG_LORA_FREQUENCY, cfg.lora_frequency);
    TEST_ASSERT_EQUAL_UINT32(APP_CFG_LOCK_TIMEOUT_MS, cfg.lock_timeout_ms);
    TEST_ASSERT_EQUAL_UINT32(APP_CFG_RELAY_DURATION_MS, cfg.relay_duration_ms);
    TEST_ASSERT_EQUAL_UINT32(APP_CFG_SLAVE_LOCKOUT_MS, cfg.slave_lockout_ms);
    TEST_ASSERT_EQUAL_UINT32(APP_CFG_ACK_TIMEOUT_MS, cfg.ack_timeout_ms);
    TEST_ASSERT_EQUAL_UINT8(APP_CFG_MAX_RETRIES, cfg.max_retries);
    TEST_ASSERT_EQUAL_UINT8(APP_CFG_MASTER_ID, cfg.master_id);
    TEST_ASSERT_EQUAL_UINT8(APP_CFG_EXPECTED_MASTER_ID, cfg.expected_master_id);
    TEST_ASSERT_FALSE(cfg.debug_mode);
    TEST_ASSERT_EQUAL_UINT32(APP_CFG_DEBUG_INTERVAL_MS, cfg.debug_interval_ms);
}

TEST_CASE("APP_CONFIG_DEFAULT respeta el modo recibido (SLAVE)", "[app_config]")
{
    app_config_t cfg = APP_CONFIG_DEFAULT(APP_DEVICE_MODE_SLAVE);
    TEST_ASSERT_EQUAL(APP_DEVICE_MODE_SLAVE, cfg.device_mode);
}

TEST_CASE("app_config_is_master distingue MASTER de SLAVE", "[app_config]")
{
    app_config_t master_cfg = APP_CONFIG_DEFAULT(APP_DEVICE_MODE_MASTER);
    app_config_t slave_cfg  = APP_CONFIG_DEFAULT(APP_DEVICE_MODE_SLAVE);

    TEST_ASSERT_TRUE(app_config_is_master(&master_cfg));
    TEST_ASSERT_FALSE(app_config_is_master(&slave_cfg));
}

TEST_CASE("app_config_is_master con NULL devuelve false", "[app_config]")
{
    TEST_ASSERT_FALSE(app_config_is_master(NULL));
}
