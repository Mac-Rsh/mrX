/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/test/mr_test.h>
#if defined(MR_USE_TEST)
#include <mrX/kernel/mr_clock.h>

static volatile mr_tick_t hook_ret;

/* Helpers */
static void test_clock_reset(void) {
    hook_ret = 0;
}

static mr_tick_t clock_test_hook(void) {
    return hook_ret;
}

/* Tests */
static void test_clock_increase(void) {
    mr_tick_t base;

    base = mr_clock_tick();
    mr_clock_increase(1);
    MR_TEST_ASSERT_NEAR(mr_clock_tick(), base, 5);
}
MR_TEST_EXPORT(clock, increase, test_clock_increase);

static void test_clock_tickless(void) {
    mr_tick_t less;
    mr_err_t ret;

    test_clock_reset();

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

static void test_clock_tick_convert(void) {
    MR_TEST_ASSERT_EQ_INT(
        MR_TICK_MSEC(1),
        (mr_tick_t)(((mr_uint64_t)1 * MR_CFG_TICK_PER_SECOND) / 1000ULL));
    MR_TEST_ASSERT_EQ_INT(MR_TICK_MSEC(1000), MR_CFG_TICK_PER_SECOND);
    MR_TEST_ASSERT_EQ_INT(
        MR_TICK_USEC(1),
        (mr_tick_t)(((mr_uint64_t)1 * MR_CFG_TICK_PER_SECOND) / 1000000ULL));
    MR_TEST_ASSERT_EQ_INT(MR_TICK_USEC(1000000), MR_CFG_TICK_PER_SECOND);
}
MR_TEST_EXPORT(clock, tick_convert, test_clock_tick_convert);

static void test_clock_uptime(void) {
    mr_uptime_t base;
    mr_uptime_t uptime;

    base = mr_clock_uptime();
    mr_clock_increase(MR_CFG_TICK_PER_SECOND);
    uptime = mr_clock_uptime();

    MR_TEST_ASSERT_EQ_UINT(uptime - base, 1000);
}
MR_TEST_EXPORT(clock, uptime, test_clock_uptime);

static void test_clock_busy_wait(void) {
    mr_clock_busy_wait(0);
    mr_clock_busy_wait(1);
}
MR_TEST_EXPORT(clock, busy_wait, test_clock_busy_wait);

#if defined(MR_USE_CLOCK_HIRES)
static void test_clock_hires_tick2timestamp_frequency(void) {
    mr_hires_timestamp_t timestamp;

    timestamp = mr_clock_hires_tick2timestamp(
        MR_CFG_HIRES_TICK_PER_SECOND, 0);

    MR_TEST_ASSERT_EQ_INT(timestamp.year, 1970);
    MR_TEST_ASSERT_EQ_INT(timestamp.month, 1);
    MR_TEST_ASSERT_EQ_INT(timestamp.day, 1);
    MR_TEST_ASSERT_EQ_INT(timestamp.hour, 0);
    MR_TEST_ASSERT_EQ_INT(timestamp.minute, 0);
    MR_TEST_ASSERT_EQ_INT(timestamp.second, 1);
    MR_TEST_ASSERT_EQ_INT(timestamp.millisecond, 0);
    MR_TEST_ASSERT_EQ_INT(timestamp.microsecond, 0);
}
MR_TEST_EXPORT(clock, hires_tick2timestamp_frequency,
               test_clock_hires_tick2timestamp_frequency);

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

static void test_clock_hires_tick2timestamp_subsecond(void) {
    const mr_hires_tick_t test_tick = 1234567ULL;
    mr_hires_timestamp_t timestamp;

    timestamp = mr_hires_tick2timestamp(test_tick, 0);
    MR_TEST_ASSERT_EQ_INT(timestamp.year, 1970);
    MR_TEST_ASSERT_EQ_INT(timestamp.month, 1);
    MR_TEST_ASSERT_EQ_INT(timestamp.day, 1);
    MR_TEST_ASSERT_EQ_INT(timestamp.hour, 0);
    MR_TEST_ASSERT_EQ_INT(timestamp.minute, 0);
    MR_TEST_ASSERT_EQ_INT(timestamp.second, 1);
    MR_TEST_ASSERT_EQ_INT(timestamp.millisecond, 234);
    MR_TEST_ASSERT_EQ_INT(timestamp.microsecond, 567);
}
MR_TEST_EXPORT(clock, hires_tick2timestamp_subsecond,
               test_clock_hires_tick2timestamp_subsecond);

static void test_clock_hires_tick2timestamp_leap_year(void) {
    const mr_hires_tick_t test_tick = 1709164800000000ULL;
    mr_hires_timestamp_t timestamp;

    timestamp = mr_hires_tick2timestamp(test_tick, 0);
    MR_TEST_ASSERT_EQ_INT(timestamp.year, 2024);
    MR_TEST_ASSERT_EQ_INT(timestamp.month, 2);
    MR_TEST_ASSERT_EQ_INT(timestamp.day, 29);
    MR_TEST_ASSERT_EQ_INT(timestamp.hour, 0);
    MR_TEST_ASSERT_EQ_INT(timestamp.minute, 0);
    MR_TEST_ASSERT_EQ_INT(timestamp.second, 0);
}
MR_TEST_EXPORT(clock, hires_tick2timestamp_leap_year,
               test_clock_hires_tick2timestamp_leap_year);

static void test_clock_hires_tick2timestamp_negative_timezone(void) {
    const mr_hires_tick_t test_tick = 1725580800000000ULL;
    mr_hires_timestamp_t timestamp;

    timestamp = mr_hires_tick2timestamp(test_tick, -5);
    MR_TEST_ASSERT_EQ_INT(timestamp.year, 2024);
    MR_TEST_ASSERT_EQ_INT(timestamp.month, 9);
    MR_TEST_ASSERT_EQ_INT(timestamp.day, 5);
    MR_TEST_ASSERT_EQ_INT(timestamp.hour, 19);
    MR_TEST_ASSERT_EQ_INT(timestamp.minute, 0);
    MR_TEST_ASSERT_EQ_INT(timestamp.second, 0);
}
MR_TEST_EXPORT(clock, hires_tick2timestamp_negative_timezone,
               test_clock_hires_tick2timestamp_negative_timezone);

static void test_clock_hires_tick2timestamp_timezone_limit(void) {
    mr_hires_timestamp_t timestamp;

    timestamp = mr_hires_tick2timestamp(0, 13);
    MR_TEST_ASSERT_EQ_INT(timestamp.year, 1970);
    MR_TEST_ASSERT_EQ_INT(timestamp.month, 1);
    MR_TEST_ASSERT_EQ_INT(timestamp.day, 1);
    MR_TEST_ASSERT_EQ_INT(timestamp.hour, 12);
    MR_TEST_ASSERT_EQ_INT(timestamp.minute, 0);
    MR_TEST_ASSERT_EQ_INT(timestamp.second, 0);

    timestamp = mr_hires_tick2timestamp(0, -13);
    MR_TEST_ASSERT_EQ_INT(timestamp.year, 1970);
    MR_TEST_ASSERT_EQ_INT(timestamp.month, 1);
    MR_TEST_ASSERT_EQ_INT(timestamp.day, 1);
    MR_TEST_ASSERT_EQ_INT(timestamp.hour, 0);
    MR_TEST_ASSERT_EQ_INT(timestamp.minute, 0);
    MR_TEST_ASSERT_EQ_INT(timestamp.second, 0);
}
MR_TEST_EXPORT(clock, hires_tick2timestamp_timezone_limit,
               test_clock_hires_tick2timestamp_timezone_limit);
#endif /* defined(MR_USE_CLOCK_HIRES) */
#endif /* defined(MR_USE_TEST) */
