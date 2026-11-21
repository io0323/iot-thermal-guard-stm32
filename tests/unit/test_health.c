/* health：全員そろったときだけリフレッシュ、1つ欠けたら呼ばない、report の期限（第7章、#39） */
#include "health.h"
#include "unity.h"

static health_t H;

void setUp(void)
{
    health_init(&H);
}
void tearDown(void) {}

static void all_checkin(void)
{
    health_checkin(&H, HEALTH_SENSE);
    health_checkin(&H, HEALTH_CONTROL);
    health_checkin(&H, HEALTH_LINK);
    health_checkin(&H, HEALTH_UI);
}

static void test_no_report_ref_yet(void)
{
    all_checkin();
    TEST_ASSERT_FALSE(health_check(&H, 100u)); /* INIT の間（基準なし）は使わない */
}

static void test_all_present_refreshes_and_clears(void)
{
    health_report_ran(&H, 1000u);
    all_checkin();
    TEST_ASSERT_TRUE(health_check(&H, 1100u));
    TEST_ASSERT_FALSE(health_check(&H, 1200u)); /* ビットは消えている */
    all_checkin();
    TEST_ASSERT_TRUE(health_check(&H, 1200u));
}

static void test_one_missing_each(void)
{
    for (int miss = 0; miss < (int)HEALTH_TASK_COUNT; miss++) {
        health_init(&H);
        health_report_ran(&H, 0u);
        for (int t = 0; t < (int)HEALTH_TASK_COUNT; t++) {
            if (t != miss) {
                health_checkin(&H, (health_task_t)t);
            }
        }
        TEST_ASSERT_FALSE(health_check(&H, 100u));
        health_checkin(&H, (health_task_t)miss); /* ビットは残っているので、そろえばリフレッシュ */
        TEST_ASSERT_TRUE(health_check(&H, 200u));
    }
}

static void test_report_deadline_1500_inclusive(void)
{
    health_report_ran(&H, 5000u);
    all_checkin();
    TEST_ASSERT_TRUE(health_check(&H, 6500u)); /* ちょうど1500ms はリフレッシュする（#39） */
    all_checkin();
    TEST_ASSERT_FALSE(health_check(&H, 6501u));
    health_report_ran(&H, 6600u);
    TEST_ASSERT_TRUE(health_check(&H, 6600u)); /* ビットは残っていた */
}

static void test_deadline_across_wrap(void)
{
    health_report_ran(&H, 0xFFFFFC00u);
    all_checkin();
    TEST_ASSERT_TRUE(health_check(&H, 0xFFFFFC00u + 1500u));
    all_checkin();
    TEST_ASSERT_FALSE(health_check(&H, 0xFFFFFC00u + 1501u));
}

static void test_clear(void)
{
    health_report_ran(&H, 0u);
    all_checkin();
    health_clear(&H);
    TEST_ASSERT_FALSE(health_check(&H, 100u));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_no_report_ref_yet);
    RUN_TEST(test_all_present_refreshes_and_clears);
    RUN_TEST(test_one_missing_each);
    RUN_TEST(test_report_deadline_1500_inclusive);
    RUN_TEST(test_deadline_across_wrap);
    RUN_TEST(test_clear);
    return UNITY_END();
}
