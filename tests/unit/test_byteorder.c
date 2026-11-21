#include "byteorder.h"
#include "sat.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static void test_u16(void)
{
    uint8_t b[2];
    put_u16le(b, 0x1234u);
    TEST_ASSERT_EQUAL_HEX8(0x34, b[0]);
    TEST_ASSERT_EQUAL_HEX8(0x12, b[1]);
    TEST_ASSERT_EQUAL_HEX16(0x1234, get_u16le(b));
    put_u16le(b, 0xFFFFu);
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, get_u16le(b));
}

static void test_i16(void)
{
    uint8_t b[2];
    put_i16le(b, -32768);
    TEST_ASSERT_EQUAL_HEX8(0x00, b[0]);
    TEST_ASSERT_EQUAL_HEX8(0x80, b[1]);
    TEST_ASSERT_EQUAL_INT16(-32768, get_i16le(b));
    put_i16le(b, -2000);
    TEST_ASSERT_EQUAL_INT16(-2000, get_i16le(b));
    put_i16le(b, 32767);
    TEST_ASSERT_EQUAL_INT16(32767, get_i16le(b));
}

static void test_u32(void)
{
    uint8_t b[4];
    put_u32le(b, 0xA1B2C3D4u);
    TEST_ASSERT_EQUAL_HEX8(0xD4, b[0]);
    TEST_ASSERT_EQUAL_HEX8(0xC3, b[1]);
    TEST_ASSERT_EQUAL_HEX8(0xB2, b[2]);
    TEST_ASSERT_EQUAL_HEX8(0xA1, b[3]);
    TEST_ASSERT_EQUAL_HEX32(0xA1B2C3D4u, get_u32le(b));
    put_u32le(b, 0xFFFFFFFFu);
    TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFFu, get_u32le(b));
}

static void test_saturating_counter(void)
{
    uint16_t c = 0xFFFEu;
    sat_inc_u16(&c);
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, c);
    sat_inc_u16(&c);
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, c);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_u16);
    RUN_TEST(test_i16);
    RUN_TEST(test_u32);
    RUN_TEST(test_saturating_counter);
    return UNITY_END();
}
