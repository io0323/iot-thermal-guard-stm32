#include <string.h>

#include "boot_id.h"
#include "crc32.h"
#include "fake_ports.h"
#include "unity.h"

static ports_t P;

void setUp(void)
{
    fake_ports_init(&P);
}
void tearDown(void) {}

static void test_crc32_of_entropy(void)
{
    const uint8_t e[4] = {0x12, 0x34, 0x56, 0x78};
    TEST_ASSERT_EQUAL_HEX32(crc32_ieee(e, 4), boot_id_make(P.entropy));
}

static void test_different_entropy_gives_different_id(void)
{
    uint32_t a = boot_id_make(P.entropy);
    g_fake.entropy[0] ^= 0x01u;
    TEST_ASSERT_NOT_EQUAL(a, boot_id_make(P.entropy));
}

static void test_zero_becomes_one(void)
{
    g_fake.entropy_len = 0u; /* CRC-32 of 空 = 0 */
    TEST_ASSERT_EQUAL_HEX32(1u, boot_id_make(P.entropy));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_crc32_of_entropy);
    RUN_TEST(test_different_entropy_gives_different_id);
    RUN_TEST(test_zero_becomes_one);
    return UNITY_END();
}
