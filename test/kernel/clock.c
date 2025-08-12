/**
 * @copyright (c) 2024-2025, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <test/mr_test.h>
#if defined(MR_USE_TEST)
#include <mr-X/mr_clock.h>

static volatile mr_tick_t hook_ret = 5;

static mr_tick_t clock_test_hook(void) {
    return hook_ret;
}

/* -------------------------------------------------------------------------- */
/* Test: Clock tick increase                                                  */
static void test_clock_increase(void) {
    mr_tick_t base;

    base = mr_clock_tick();
    mr_clock_increase(1);
    MR_TEST_ASSERT_NEAR(mr_clock_tick(), base, 100);
}
MR_TEST_EXPORT(clock, increase, test_clock_increase);

/* -------------------------------------------------------------------------- */
/* Test: Clock hook add and tickless                                          */
static void test_clock_tickless(void) {
    MR_TEST_ASSERT_EQ_INT(mr_clock_hook_add(clock_test_hook), 0);

    hook_ret = 3;
    MR_TEST_ASSERT_EQ_INT(mr_clock_tickless(), 3);

    hook_ret = 7;
    MR_TEST_ASSERT_EQ_INT(mr_clock_tickless(), 7);
}
MR_TEST_EXPORT(clock, tickless, test_clock_tickless);
#endif /* defined(MR_USE_TEST) */
