/* リセット要因の判定（bsp/stm32f4/reset_decode.c、詳細設計書 第7章、open-questions #51） */
#include "reset_decode.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static void test_power_on_sets_por_bor_pin(void)
{
    TEST_ASSERT_EQUAL_INT(RESET_POWER_ON, reset_decode_csr(RESET_CSR_PORRSTF | RESET_CSR_BORRSTF | RESET_CSR_PINRSTF));
}

static void test_brownout_only(void)
{
    TEST_ASSERT_EQUAL_INT(RESET_BROWNOUT, reset_decode_csr(RESET_CSR_BORRSTF | RESET_CSR_PINRSTF));
}

static void test_watchdog(void)
{
    TEST_ASSERT_EQUAL_INT(RESET_WATCHDOG, reset_decode_csr(RESET_CSR_IWDGRSTF | RESET_CSR_PINRSTF));
}

static void test_software(void)
{
    TEST_ASSERT_EQUAL_INT(RESET_SOFTWARE, reset_decode_csr(RESET_CSR_SFTRSTF | RESET_CSR_PINRSTF));
}

static void test_pin_only(void)
{
    TEST_ASSERT_EQUAL_INT(RESET_PIN, reset_decode_csr(RESET_CSR_PINRSTF));
}

static void test_none_is_unknown(void)
{
    TEST_ASSERT_EQUAL_INT(RESET_UNKNOWN, reset_decode_csr(0U));
}

/* 優先順の残り：低電力は BOR より後、ピンより先。IWDG はソフトウェアより先 */
static void test_remaining_order(void)
{
    TEST_ASSERT_EQUAL_INT(RESET_LOW_POWER, reset_decode_csr(RESET_CSR_LPWRRSTF | RESET_CSR_PINRSTF));
    TEST_ASSERT_EQUAL_INT(RESET_BROWNOUT, reset_decode_csr(RESET_CSR_BORRSTF | RESET_CSR_LPWRRSTF));
    TEST_ASSERT_EQUAL_INT(RESET_WATCHDOG, reset_decode_csr(RESET_CSR_IWDGRSTF | RESET_CSR_SFTRSTF));
    TEST_ASSERT_EQUAL_INT(RESET_UNKNOWN, reset_decode_csr(1UL << 30U)); /* WWDG は使わない */
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_power_on_sets_por_bor_pin);
    RUN_TEST(test_brownout_only);
    RUN_TEST(test_watchdog);
    RUN_TEST(test_software);
    RUN_TEST(test_pin_only);
    RUN_TEST(test_none_is_unknown);
    RUN_TEST(test_remaining_order);
    return UNITY_END();
}
