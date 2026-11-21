#include "crc16.h"
#include "crc32.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static const uint8_t check[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};

static void test_crc16_check_value(void)
{
    TEST_ASSERT_EQUAL_HEX16(0x29B1, crc16_ccitt_false(check, sizeof check));
}

static void test_crc16_empty_is_init(void)
{
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, crc16_ccitt_false(NULL, 0));
}

static void test_crc16_golden_heartbeat(void)
{
    /* ゴールデン09：type 03、seq 0、len 4、uptime 123456 → CRC 0xE3B1 */
    const uint8_t body[] = {0x03, 0x00, 0x00, 0x04, 0x40, 0xE2, 0x01, 0x00};
    TEST_ASSERT_EQUAL_HEX16(0xE3B1, crc16_ccitt_false(body, sizeof body));
}

static void test_crc32_check_value(void)
{
    TEST_ASSERT_EQUAL_HEX32(0xCBF43926u, crc32_ieee(check, sizeof check));
}

static void test_crc32_empty(void)
{
    TEST_ASSERT_EQUAL_HEX32(0x00000000u, crc32_ieee(NULL, 0));
}

static void test_crc32_single_bit_changes(void)
{
    uint8_t a[4] = {0, 0, 0, 0};
    uint8_t b[4] = {0, 0, 0, 1};
    TEST_ASSERT_NOT_EQUAL(crc32_ieee(a, 4), crc32_ieee(b, 4));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_crc16_check_value);
    RUN_TEST(test_crc16_empty_is_init);
    RUN_TEST(test_crc16_golden_heartbeat);
    RUN_TEST(test_crc32_check_value);
    RUN_TEST(test_crc32_empty);
    RUN_TEST(test_crc32_single_bit_changes);
    return UNITY_END();
}
