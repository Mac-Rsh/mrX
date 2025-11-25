/**
 * @copyright (c) 2024-2025, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/test/mr_test.h>
#if defined(MR_USE_TEST)
#include <mrX/kernel/mr_clock.h>

static volatile mr_tick_t hook_ret;

/* -------------------------------------------------------------------------- */
/* Utilities: hook function                                                   */
static mr_tick_t clock_test_hook(void) {
    return hook_ret;
}

/* -------------------------------------------------------------------------- */
/* Test: Clock tick increase                                                  */
static void test_clock_increase(void) {
    mr_tick_t base;

    base = mr_clock_tick();
    mr_clock_increase(1);
    MR_TEST_ASSERT_NEAR(mr_clock_tick(), base, 5);
}
MR_TEST_EXPORT(clock, increase, test_clock_increase);

/* -------------------------------------------------------------------------- */
/* Test: Clock hook add and tickless                                          */
static void test_clock_tickless(void) {
    mr_tick_t less;
    mr_err_t ret;

    ret = mr_clock_hook_add(clock_test_hook);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    hook_ret = 3;
    less = mr_clock_tickless();
    MR_TEST_ASSERT_EQ_INT(less, 3);

    hook_ret = 7;
    less = mr_clock_tickless();
    MR_TEST_ASSERT_EQ_INT(less, 7);
}
MR_TEST_EXPORT(clock, tickless, test_clock_tickless);

#if defined(MR_USE_CLOCK_HIRES)
/* Test: High resolution tick */
static void test_clock_hires_tick(void) {
    mr_hires_tick_t initial_tick, current_tick;

    initial_tick = mr_clock_hires_tick();
    mr_clock_hires_tick_set(initial_tick + 10000);
    current_tick = mr_clock_hires_tick();
    MR_TEST_ASSERT_NEAR(current_tick, initial_tick + 10000, 10);
}
MR_TEST_EXPORT(clock, hires_tick, test_clock_hires_tick);

/* Test: High resolution tick to timestamp conversion */
static void test_clock_hires_tick2timestamp(void) {
    const mr_hires_tick_t test_tick = 1725580800000000ULL;
    mr_hires_timestamp_t timestamp;
    const mr_uint8_t time_zone = 8;

    timestamp = mr_hires_tick2timestamp(test_tick, time_zone);
    MR_TEST_ASSERT_EQ_INT(timestamp.year, 2024);
    MR_TEST_ASSERT_EQ_INT(timestamp.month, 9);
    MR_TEST_ASSERT_EQ_INT(timestamp.day, 6);
    MR_TEST_ASSERT_EQ_INT(timestamp.hour, 8);
    MR_TEST_ASSERT_EQ_INT(timestamp.minute, 0);
    MR_TEST_ASSERT_EQ_INT(timestamp.second, 0);
}
MR_TEST_EXPORT(clock, hires_tick2timestamp, test_clock_hires_tick2timestamp);
#endif /* defined(MR_USE_CLOCK_HIRES) */
#endif /* defined(MR_USE_TEST) */
