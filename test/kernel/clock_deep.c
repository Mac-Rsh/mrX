/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2026-08-30        MacRsh        Deep test version
 */

#include <mrX/test/mr_test.h>
#if defined(MR_USE_TEST)
#include <mrX/kernel/mr_clock.h>

/* Helpers */
static mr_tick_t deep_clock_hook(void) {
    /* A constant positive tickless constraint */
    return 7;
}

/* Tests */

/* Test: microsecond to tick conversion rounds down at every boundary */
MR_TEST(clock, deep_tick_convert_usec_rounding) {
    MR_TEST_ASSERT_EQ_UINT(MR_TICK_USEC(0), 0);
    MR_TEST_ASSERT_EQ_UINT(MR_TICK_USEC(1), 0);
    MR_TEST_ASSERT_EQ_UINT(
        MR_TICK_USEC(999),
        (mr_tick_t)(((mr_uint64_t)999 * MR_CFG_TICK_PER_SECOND) / 1000000ULL));
    MR_TEST_ASSERT_EQ_UINT(MR_TICK_USEC(1000), 1);
    MR_TEST_ASSERT_EQ_UINT(
        MR_TICK_USEC(1500),
        (mr_tick_t)(((mr_uint64_t)1500 * MR_CFG_TICK_PER_SECOND) / 1000000ULL));
    MR_TEST_ASSERT_EQ_UINT(
        MR_TICK_USEC(1999),
        (mr_tick_t)(((mr_uint64_t)1999 * MR_CFG_TICK_PER_SECOND) / 1000000ULL));
    MR_TEST_ASSERT_EQ_UINT(MR_TICK_USEC(2000), 2);
}

/* Test: millisecond to tick conversion follows the scale formula */
MR_TEST(clock, deep_tick_convert_msec_formula) {
    MR_TEST_ASSERT_EQ_UINT(MR_TICK_MSEC(0), 0);
    MR_TEST_ASSERT_EQ_UINT(
        MR_TICK_MSEC(1),
        (mr_tick_t)(((mr_uint64_t)1 * MR_CFG_TICK_PER_SECOND) / 1000ULL));
    MR_TEST_ASSERT_EQ_UINT(
        MR_TICK_MSEC(999),
        (mr_tick_t)(((mr_uint64_t)999 * MR_CFG_TICK_PER_SECOND) / 1000ULL));
    MR_TEST_ASSERT_EQ_UINT(
        MR_TICK_MSEC(1500),
        (mr_tick_t)(((mr_uint64_t)1500 * MR_CFG_TICK_PER_SECOND) / 1000ULL));
    MR_TEST_ASSERT_EQ_UINT(MR_TICK_MSEC(MR_CFG_TICK_PER_SECOND),
                           MR_CFG_TICK_PER_SECOND);
}

/* Test: uptime advances by at least the increased ticks */
MR_TEST(clock, deep_uptime_small_increase) {
    mr_uptime_t base, now;
    mr_uint64_t diff;

    base = mr_clock_uptime();
    mr_clock_increase(5);
    now = mr_clock_uptime();
    diff = now - base;

    /* Background ticks may only add time, never remove it */
    MR_TEST_EXPECT_GE_UINT(diff, 5);
    MR_TEST_EXPECT_LE_UINT(diff, 5 + 1000);
}

/* Test: hires timestamp of the unix epoch is exactly zero time */
MR_TEST(clock, deep_hires_timestamp_epoch) {
    mr_hires_timestamp_t timestamp;

    timestamp = mr_hires_tick2timestamp(0, 0);
    MR_TEST_ASSERT_EQ_INT(timestamp.year, 1970);
    MR_TEST_ASSERT_EQ_INT(timestamp.month, 1);
    MR_TEST_ASSERT_EQ_INT(timestamp.day, 1);
    MR_TEST_ASSERT_EQ_INT(timestamp.hour, 0);
    MR_TEST_ASSERT_EQ_INT(timestamp.minute, 0);
    MR_TEST_ASSERT_EQ_INT(timestamp.second, 0);
    MR_TEST_ASSERT_EQ_INT(timestamp.millisecond, 0);
    MR_TEST_ASSERT_EQ_INT(timestamp.microsecond, 0);
}

/* Test: 2000 is a leap year so 2000-02-29 must exist */
MR_TEST(clock, deep_hires_timestamp_leap_century) {
    mr_hires_timestamp_t timestamp;

    /* 951782400 seconds since epoch is 2000-02-29 00:00:00 UTC */
    timestamp = mr_hires_tick2timestamp(951782400000000ULL, 0);
    MR_TEST_ASSERT_EQ_INT(timestamp.year, 2000);
    MR_TEST_ASSERT_EQ_INT(timestamp.month, 2);
    MR_TEST_ASSERT_EQ_INT(timestamp.day, 29);
    MR_TEST_ASSERT_EQ_INT(timestamp.hour, 0);

    /* One day later is 2000-03-01 */
    timestamp = mr_hires_tick2timestamp(951868800000000ULL, 0);
    MR_TEST_ASSERT_EQ_INT(timestamp.year, 2000);
    MR_TEST_ASSERT_EQ_INT(timestamp.month, 3);
    MR_TEST_ASSERT_EQ_INT(timestamp.day, 1);
}

/* Test: 2100 is not a leap year so February has 28 days */
MR_TEST(clock, deep_hires_timestamp_nonleap_century) {
    mr_hires_timestamp_t timestamp;

    /* 4107542400 seconds since epoch is 2100-03-01 00:00:00 UTC */
    timestamp = mr_hires_tick2timestamp(4107542400000000ULL, 0);
    MR_TEST_ASSERT_EQ_INT(timestamp.year, 2100);
    MR_TEST_ASSERT_EQ_INT(timestamp.month, 3);
    /* Potential bug: a %4 only leap check would report 2100-02-29 */
    MR_TEST_ASSERT_EQ_INT(timestamp.day, 1);
}

/* Test: the year rollover boundary converts to adjacent days */
MR_TEST(clock, deep_hires_timestamp_year_rollover) {
    mr_hires_timestamp_t timestamp;

    /* The last microsecond of 1970 */
    timestamp = mr_hires_tick2timestamp(31535999999999ULL, 0);
    MR_TEST_ASSERT_EQ_INT(timestamp.year, 1970);
    MR_TEST_ASSERT_EQ_INT(timestamp.month, 12);
    MR_TEST_ASSERT_EQ_INT(timestamp.day, 31);
    MR_TEST_ASSERT_EQ_INT(timestamp.hour, 23);
    MR_TEST_ASSERT_EQ_INT(timestamp.minute, 59);
    MR_TEST_ASSERT_EQ_INT(timestamp.second, 59);
    MR_TEST_ASSERT_EQ_INT(timestamp.millisecond, 999);
    MR_TEST_ASSERT_EQ_INT(timestamp.microsecond, 999);

    /* The first microsecond of 1971 */
    timestamp = mr_hires_tick2timestamp(31536000000000ULL, 0);
    MR_TEST_ASSERT_EQ_INT(timestamp.year, 1971);
    MR_TEST_ASSERT_EQ_INT(timestamp.month, 1);
    MR_TEST_ASSERT_EQ_INT(timestamp.day, 1);
    MR_TEST_ASSERT_EQ_INT(timestamp.second, 0);
}

/* Test: subsecond fields split correctly around one second edges */
MR_TEST(clock, deep_hires_timestamp_subsecond_edges) {
    mr_hires_timestamp_t timestamp;

    timestamp = mr_hires_tick2timestamp(1999999ULL, 0);
    MR_TEST_ASSERT_EQ_INT(timestamp.second, 1);
    MR_TEST_ASSERT_EQ_INT(timestamp.millisecond, 999);
    MR_TEST_ASSERT_EQ_INT(timestamp.microsecond, 999);

    timestamp = mr_hires_tick2timestamp(2000000ULL, 0);
    MR_TEST_ASSERT_EQ_INT(timestamp.second, 2);
    MR_TEST_ASSERT_EQ_INT(timestamp.millisecond, 0);
    MR_TEST_ASSERT_EQ_INT(timestamp.microsecond, 0);
}

/* Test: positive time zones shift within and across the clamp limit */
MR_TEST(clock, deep_hires_timestamp_timezone_positive) {
    mr_hires_timestamp_t timestamp;

    /* One hour UTC with UTC +12 is 13:00 the same day */
    timestamp = mr_hires_tick2timestamp(3600000000ULL, 12);
    MR_TEST_ASSERT_EQ_INT(timestamp.year, 1970);
    MR_TEST_ASSERT_EQ_INT(timestamp.month, 1);
    MR_TEST_ASSERT_EQ_INT(timestamp.day, 1);
    MR_TEST_ASSERT_EQ_INT(timestamp.hour, 13);

    /* Out of range zones clamp to +12 and give the same result */
    timestamp = mr_hires_tick2timestamp(3600000000ULL, 24);
    MR_TEST_ASSERT_EQ_INT(timestamp.hour, 13);
}

/* Test: negative local time is clamped to the epoch */
MR_TEST(clock, deep_hires_timestamp_timezone_negative) {
    mr_hires_timestamp_t timestamp;

    /* One hour UTC with UTC -2 falls below the supported epoch */
    timestamp = mr_hires_tick2timestamp(3600000000ULL, -2);
    MR_TEST_EXPECT_EQ_INT(timestamp.year, 1970);
    MR_TEST_EXPECT_EQ_INT(timestamp.month, 1);
    MR_TEST_EXPECT_EQ_INT(timestamp.day, 1);
    MR_TEST_EXPECT_EQ_INT(timestamp.hour, 0);
}

/* Test: a time zone that shifts the date backwards across a month */
MR_TEST(clock, deep_hires_timestamp_timezone_day_shift) {
    mr_hires_timestamp_t timestamp;

    /* 2024-09-06 00:00 UTC with UTC -8 is 2024-09-05 16:00 */
    timestamp = mr_hires_tick2timestamp(1725580800000000ULL, -8);
    MR_TEST_ASSERT_EQ_INT(timestamp.year, 2024);
    MR_TEST_ASSERT_EQ_INT(timestamp.month, 9);
    MR_TEST_ASSERT_EQ_INT(timestamp.day, 5);
    MR_TEST_ASSERT_EQ_INT(timestamp.hour, 16);
    MR_TEST_ASSERT_EQ_INT(timestamp.minute, 0);
}

/* Test: the clock hook table has a strictly limited number of slots */
MR_TEST(clock, deep_hook_capacity) {
    mr_err_t ret;

    ret = mr_clock_hook_add(deep_clock_hook);
    if (ret == 0) {
        /* A registered hook must bound the reported tickless value */
        MR_TEST_EXPECT_LE_UINT(mr_clock_tickless(), 7);
    } else {
        /* With MR_CFG_CLOCK_HOOK_SIZE 2 the kernel timer and earlier
           tests already occupy every slot */
        MR_TEST_EXPECT_EQ_INT(ret, -MR_ENOMEM);
    }
}

/* Test: the 32-bit tick wraps while uptime keeps counting */
MR_TEST(clock, deep_tick_wraparound_uptime) {
    mr_uptime_t up_before, up_after;
    mr_tick_t t_before, delta, t_after;
    mr_uint64_t diff;

    t_before = mr_clock_tick();
    up_before = mr_clock_uptime();

    /* Advance exactly ten ticks past the 32-bit wrap boundary */
    delta = (mr_tick_t)(0U - t_before) + 10U;
    mr_clock_increase(delta);

    t_after = mr_clock_tick();
    up_after = mr_clock_uptime();
    diff = up_after - up_before;

    /* The tick counter must have wrapped into a small value */
    MR_TEST_EXPECT_LT_UINT(t_after, 1000);
    /* Uptime must keep counting milliseconds across the wrap */
    MR_TEST_EXPECT_GE_UINT(diff, (mr_uint64_t)delta);
    MR_TEST_EXPECT_LE_UINT(diff, (mr_uint64_t)delta + 5000U);
}

#endif /* defined(MR_USE_TEST) */
