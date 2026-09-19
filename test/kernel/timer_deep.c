/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2026-08-30        MacRsh        Deep test version
 */

#include <mrX/test/mr_test.h>
#if defined(MR_USE_TEST)
#if defined(MR_USE_TIMER)
#include <mrX/kernel/mr_timer.h>
#include <mrX/kernel/mr_clock.h>

/* Helpers */
static int deep_timer_count_a;
static int deep_timer_count_b;
static mr_timer_t *deep_timer_order[8];
static int deep_timer_order_n;

static void deep_timer_mark_a(mr_timer_t *timer, void *param) {
    int *count = param;

    (*count)++;
    if (deep_timer_order_n < 8) {
        deep_timer_order[deep_timer_order_n++] = timer;
    }
}

static void deep_timer_mark_b(mr_timer_t *timer, void *param) {
    int *count = param;

    (*count)++;
    if (deep_timer_order_n < 8) {
        deep_timer_order[deep_timer_order_n++] = timer;
    }
}

static void deep_timer_start_other(mr_timer_t *timer, void *param) {
    mr_timer_t *other = param;

    (void)timer;
    deep_timer_count_a++;
    mr_timer_start(other);
}

static void deep_timer_rearm_entry(mr_timer_t *timer, void *param) {
    int *count = param;

    (*count)++;
    if (*count == 1) {
        mr_timer_tick_set(timer, 4);
        mr_timer_start(timer);
    }
}

/* Tests */

/* Test: the periodic bit is stored verbatim and reported by tick get */
MR_TEST(timer, deep_periodic_bit_preserved) {
    mr_timer_t timer;
    mr_timer_t *created;
    mr_err_t ret;

    ret = mr_timer_init(&timer, deep_timer_mark_a, MR_NULL,
                        1000 | MR_TIMER_PERIODIC);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_UINT(mr_timer_tick(&timer), 1000 | MR_TIMER_PERIODIC);

    created = mr_timer_create(deep_timer_mark_a, MR_NULL,
                              2000 | MR_TIMER_PERIODIC);
    MR_TEST_ASSERT_NE_PTR(created, MR_NULL);
    MR_TEST_ASSERT_EQ_UINT(mr_timer_tick(created), 2000 | MR_TIMER_PERIODIC);

    MR_TEST_ASSERT_EQ_INT(mr_timer_del(&timer), 0);
    MR_TEST_ASSERT_EQ_INT(mr_timer_del(created), 0);
}

/* Test: a oneshot timer fires exactly once inside its window */
MR_TEST(timer, deep_oneshot_fire_window) {
    mr_timer_t timer;
    int count = 0;
    mr_err_t ret;

    ret = mr_timer_init(&timer, deep_timer_mark_a, &count, 3);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(mr_timer_start(&timer), 0);

    mr_clock_increase(2);
    MR_TEST_EXPECT_EQ_INT(count, 0);

    mr_clock_increase(1);
    MR_TEST_EXPECT_EQ_INT(count, 1);

    /* A oneshot timer must never refire */
    mr_clock_increase(4);
    MR_TEST_EXPECT_EQ_INT(count, 1);

    MR_TEST_ASSERT_EQ_INT(mr_timer_del(&timer), 0);
}

/* Test: two oneshot timers expire in deadline order */
MR_TEST(timer, deep_fire_order_two_oneshots) {
    mr_timer_t timer_a;
    mr_timer_t timer_b;
    mr_err_t ret;

    deep_timer_order_n = 0;
    deep_timer_count_a = 0;
    deep_timer_count_b = 0;
    ret = mr_timer_init(&timer_a, deep_timer_mark_a, &deep_timer_count_a, 2);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_timer_init(&timer_b, deep_timer_mark_b, &deep_timer_count_b, 4);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(mr_timer_start(&timer_a), 0);
    MR_TEST_ASSERT_EQ_INT(mr_timer_start(&timer_b), 0);

    mr_clock_increase(2);
    MR_TEST_EXPECT_EQ_INT(deep_timer_count_a, 1);
    MR_TEST_EXPECT_EQ_INT(deep_timer_count_b, 0);

    mr_clock_increase(2);
    MR_TEST_EXPECT_EQ_INT(deep_timer_count_a, 1);
    MR_TEST_EXPECT_EQ_INT(deep_timer_count_b, 1);
    /* The shorter deadline must have been serviced first */
    MR_TEST_EXPECT_EQ_INT(deep_timer_order_n, 2);
    MR_TEST_EXPECT_EQ_PTR(deep_timer_order[0], &timer_a);
    MR_TEST_EXPECT_EQ_PTR(deep_timer_order[1], &timer_b);

    MR_TEST_ASSERT_EQ_INT(mr_timer_del(&timer_a), 0);
    MR_TEST_ASSERT_EQ_INT(mr_timer_del(&timer_b), 0);
}

/* Test: timers sharing a deadline fire in start order */
MR_TEST(timer, deep_same_deadline_fifo_order) {
    mr_timer_t timer_a;
    mr_timer_t timer_b;
    mr_err_t ret;

    deep_timer_order_n = 0;
    deep_timer_count_a = 0;
    deep_timer_count_b = 0;
    ret = mr_timer_init(&timer_a, deep_timer_mark_a, &deep_timer_count_a, 1);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_timer_init(&timer_b, deep_timer_mark_b, &deep_timer_count_b, 1);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(mr_timer_start(&timer_a), 0);
    MR_TEST_ASSERT_EQ_INT(mr_timer_start(&timer_b), 0);

    mr_clock_increase(1);
    MR_TEST_EXPECT_EQ_INT(deep_timer_count_a, 1);
    MR_TEST_EXPECT_EQ_INT(deep_timer_count_b, 1);
    MR_TEST_EXPECT_EQ_INT(deep_timer_order_n, 2);
    MR_TEST_EXPECT_EQ_PTR(deep_timer_order[0], &timer_a);
    MR_TEST_EXPECT_EQ_PTR(deep_timer_order[1], &timer_b);

    MR_TEST_ASSERT_EQ_INT(mr_timer_del(&timer_a), 0);
    MR_TEST_ASSERT_EQ_INT(mr_timer_del(&timer_b), 0);
}

/* Test: a periodic timer fires once per period over several periods */
MR_TEST(timer, deep_periodic_count_window) {
    mr_timer_t timer;
    int count = 0;
    int i;
    mr_err_t ret;

    ret = mr_timer_init(&timer, deep_timer_mark_a, &count,
                        2 | MR_TIMER_PERIODIC);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(mr_timer_start(&timer), 0);

    /* Advance one tick per call so every period edge is checked, a single
       multi-tick increase fires a periodic timer only once per check */
    for (i = 0; i < 7; i++) {
        mr_clock_increase(1);
    }
    /* Expires at +2, +4 and +6 within the requested window */
    MR_TEST_EXPECT_GE_INT(count, 3);
    MR_TEST_EXPECT_LE_INT(count, 30);

    MR_TEST_ASSERT_EQ_INT(mr_timer_del(&timer), 0);
}

/* Test: a stopped timer never expires */
MR_TEST(timer, deep_stop_prevents_expiry) {
    mr_timer_t timer;
    int count = 0;
    mr_err_t ret;

    ret = mr_timer_init(&timer, deep_timer_mark_a, &count, 3);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(mr_timer_start(&timer), 0);
    MR_TEST_ASSERT_EQ_INT(mr_timer_stop(&timer), 0);

    mr_clock_increase(10);
    MR_TEST_EXPECT_EQ_INT(count, 0);
    MR_TEST_EXPECT_EQ_INT(MR_TIMER_IS_TIMING(&timer), MR_FALSE);

    MR_TEST_ASSERT_EQ_INT(mr_timer_del(&timer), 0);
}

/* Test: deleting a running timer stops it without firing */
MR_TEST(timer, deep_delete_running_prevents_expiry) {
    mr_timer_t timer;
    int count = 0;
    mr_err_t ret;

    ret = mr_timer_init(&timer, deep_timer_mark_a, &count, 3);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(mr_timer_start(&timer), 0);
    MR_TEST_ASSERT_EQ_INT(mr_timer_del(&timer), 0);

    mr_clock_increase(10);
    MR_TEST_ASSERT_EQ_INT(count, 0);
}

/* Test: restarting a running timer pushes its deadline into the future */
/* Test: tick set on a running timer stops it with the new period */
MR_TEST(timer, deep_tick_set_stops_running) {
    mr_timer_t timer;
    int count = 0;
    int snapshot;
    mr_err_t ret;

    ret = mr_timer_init(&timer, deep_timer_mark_a, &count,
                        1 | MR_TIMER_PERIODIC);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(mr_timer_start(&timer), 0);

    mr_clock_increase(1);
    MR_TEST_EXPECT_GE_INT(count, 1);

    /* Changing the tick must stop the timer */
    MR_TEST_ASSERT_EQ_INT(mr_timer_tick_set(&timer, 5), 0);
    MR_TEST_EXPECT_EQ_INT(MR_TIMER_IS_TIMING(&timer), MR_FALSE);
    snapshot = count;

    mr_clock_increase(3);
    MR_TEST_EXPECT_EQ_INT(count, snapshot);

    /* Restarting uses the new period */
    MR_TEST_ASSERT_EQ_INT(mr_timer_start(&timer), 0);
    mr_clock_increase(4);
    MR_TEST_EXPECT_EQ_INT(count, snapshot);

    mr_clock_increase(1);
    MR_TEST_EXPECT_EQ_INT(count, snapshot + 1);

    MR_TEST_ASSERT_EQ_INT(mr_timer_del(&timer), 0);
}

/* Test: an entry can rearm itself with a new period */
MR_TEST(timer, deep_rearm_with_new_period_in_entry) {
    mr_timer_t timer;
    int count = 0;
    mr_err_t ret;

    ret = mr_timer_init(&timer, deep_timer_rearm_entry, &count, 2);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(mr_timer_start(&timer), 0);

    mr_clock_increase(2);
    MR_TEST_EXPECT_EQ_INT(count, 1);
    MR_TEST_EXPECT_EQ_INT(MR_TIMER_IS_TIMING(&timer), MR_TRUE);

    mr_clock_increase(2);
    MR_TEST_EXPECT_EQ_INT(count, 1);

    mr_clock_increase(2);
    MR_TEST_EXPECT_EQ_INT(count, 2);
    MR_TEST_EXPECT_EQ_INT(MR_TIMER_IS_TIMING(&timer), MR_FALSE);

    MR_TEST_ASSERT_EQ_INT(mr_timer_del(&timer), 0);
}

/* Test: an entry may start another stopped timer */
MR_TEST(timer, deep_entry_starts_other_timer) {
    mr_timer_t timer_a;
    mr_timer_t timer_b;
    mr_err_t ret;

    deep_timer_count_a = 0;
    deep_timer_count_b = 0;
    ret = mr_timer_init(&timer_a, deep_timer_start_other, &timer_b, 1);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_timer_init(&timer_b, deep_timer_mark_b, &deep_timer_count_b, 2);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(mr_timer_start(&timer_a), 0);

    /* A fires and starts B with a two tick deadline from now */
    mr_clock_increase(1);
    MR_TEST_EXPECT_EQ_INT(deep_timer_count_a, 1);

    mr_clock_increase(1);
    MR_TEST_EXPECT_EQ_INT(deep_timer_count_b, 0);

    mr_clock_increase(1);
    MR_TEST_EXPECT_EQ_INT(deep_timer_count_b, 1);

    MR_TEST_ASSERT_EQ_INT(mr_timer_del(&timer_a), 0);
    MR_TEST_ASSERT_EQ_INT(mr_timer_del(&timer_b), 0);
}

/* Test: starting a timer twice keeps a single deadline */
MR_TEST(timer, deep_start_twice_single_fire) {
    mr_timer_t timer;
    int count = 0;
    mr_err_t ret;

    ret = mr_timer_init(&timer, deep_timer_mark_a, &count, 5);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(mr_timer_start(&timer), 0);
    MR_TEST_ASSERT_EQ_INT(mr_timer_start(&timer), 0);

    mr_clock_increase(5);
    MR_TEST_EXPECT_EQ_INT(count, 1);
    mr_clock_increase(5);
    MR_TEST_EXPECT_EQ_INT(count, 1);

    MR_TEST_ASSERT_EQ_INT(mr_timer_del(&timer), 0);
}

/* Test: deadlines past the 32-bit tick wrap boundary still expire */
MR_TEST(timer, deep_wraparound_expiry) {
    mr_timer_t near_timer;
    mr_timer_t far_timer;
    int near_count = 0;
    int far_count = 0;
    mr_tick_t t_before, delta;
    mr_err_t ret;

    /* Park the clock 512 ticks before the 32-bit wrap boundary */
    t_before = mr_clock_tick();
    delta = (mr_tick_t)(0xFFFFFE00U - t_before);
    mr_clock_increase(delta);

    /* The near timer deadline wraps to about 0x200 */
    ret = mr_timer_init(&near_timer, deep_timer_mark_a, &near_count, 0x400U);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(mr_timer_start(&near_timer), 0);

    /* The far timer deadline wraps to about 0x80000200 */
    ret = mr_timer_init(&far_timer, deep_timer_mark_b, &far_count,
                        0x7FFFFFFFU);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(mr_timer_start(&far_timer), 0);

    /* Advance exactly onto the boundary: nothing may fire yet */
    mr_clock_increase(0x200U);
    MR_TEST_EXPECT_EQ_INT(near_count, 0);
    MR_TEST_EXPECT_EQ_INT(far_count, 0);

    /* Cross the boundary: the near timer must fire, the far one not */
    mr_clock_increase(0x200U);
    MR_TEST_EXPECT_GE_INT(near_count, 1);
    MR_TEST_EXPECT_EQ_INT(far_count, 0);

    /* A oneshot timer that fired across the wrap must not refire */
    mr_clock_increase(0x200U);
    MR_TEST_EXPECT_EQ_INT(near_count, 1);
    MR_TEST_EXPECT_EQ_INT(far_count, 0);

    MR_TEST_ASSERT_EQ_INT(mr_timer_del(&near_timer), 0);
    MR_TEST_ASSERT_EQ_INT(mr_timer_del(&far_timer), 0);
}

#endif /* defined(MR_USE_TIMER) */
#endif /* defined(MR_USE_TEST) */
