/**
 * @copyright (c) 2024-2025, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/test/mr_test.h>
#if defined(MR_USE_TEST)
#if defined(MR_USE_TIMER)
#include <mrX/kernel/mr_timer.h>

static mr_timer_t *last_timer = MR_NULL;
static void *last_param = MR_NULL;

static void test_timer_entry(mr_timer_t *timer, void *param) {
    last_timer = timer;
    last_param = param;
}

/* -------------------------------------------------------------------------- */
/* Test: Timer init / del */
static void test_timer_init_del(void) {
    mr_timer_t timer;
    mr_err_t ret;

    ret = mr_timer_init(&timer, test_timer_entry, (void *)0x1234, 1000);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(MR_OBJECT_IS_INITED(&timer), MR_TRUE);
    MR_TEST_ASSERT_EQ_UINT(timer.itick, 1000);

    ret = mr_timer_del(&timer);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(MR_OBJECT_IS_INITED(&timer), MR_FALSE);
}
MR_TEST_EXPORT(timer, init_del, test_timer_init_del);

/* -------------------------------------------------------------------------- */
/* Test: Timer create / del */
static void test_timer_create_del(void) {
    mr_timer_t *timer;
    mr_err_t ret;

    timer = mr_timer_create(test_timer_entry, (void *)0x5678, 2000);
    MR_TEST_ASSERT_NE_PTR(timer, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(MR_OBJECT_IS_INITED(timer), MR_TRUE);
    MR_TEST_ASSERT_EQ_UINT(timer->itick, 2000);

    ret = mr_timer_del(timer);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
}
MR_TEST_EXPORT(timer, create_del, test_timer_create_del);

/* -------------------------------------------------------------------------- */
/* Test: Timer start / stop */
static void test_timer_start_stop(void) {
    mr_timer_t timer, timer1;
    mr_err_t ret;

    ret = mr_timer_init(&timer, test_timer_entry, MR_NULL, 2);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    ret = mr_timer_start(&timer);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    ret = mr_timer_init(&timer1, test_timer_entry, MR_NULL, 2);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_timer_start(&timer1);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_timer_del(&timer1);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    ret = mr_timer_init(&timer1, test_timer_entry, MR_NULL, 1);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_timer_start(&timer1);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_timer_del(&timer1);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    last_timer = MR_NULL;
    mr_clock_increase(2);
    MR_TEST_ASSERT_EQ_PTR(last_timer, &timer);

    ret = mr_timer_stop(&timer);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    ret = mr_timer_del(&timer);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
}
MR_TEST_EXPORT(timer, start_stop, test_timer_start_stop);

/* -------------------------------------------------------------------------- */
/* Test: Timer tick get / set */
static void test_timer_tick_get_set(void) {
    mr_timer_t timer;
    mr_err_t ret;
    mr_tick_t tick;

    ret = mr_timer_init(&timer, test_timer_entry, MR_NULL, 500);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    tick = mr_timer_tick(&timer);
    MR_TEST_ASSERT_EQ_UINT(tick, 500);

    ret = mr_timer_tick_set(&timer, 1500);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    tick = mr_timer_tick(&timer);
    MR_TEST_ASSERT_EQ_UINT(tick, 1500);

    ret = mr_timer_del(&timer);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
}
MR_TEST_EXPORT(timer, tick_get_set, test_timer_tick_get_set);

/* -------------------------------------------------------------------------- */
/* Test: Timer periodic mode */
static void test_timer_periodic(void) {
    mr_timer_t timer;
    mr_err_t ret;
    int i;

    ret = mr_timer_init(&timer, test_timer_entry, (void *)0x1111,
                        1 | MR_TIMER_PERIODIC);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    ret = mr_timer_start(&timer);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    for (i = 0; i < 10; i++) {
        last_timer = MR_NULL;
        last_param = MR_NULL;
        mr_clock_increase(1);
        MR_TEST_ASSERT_EQ_PTR(last_timer, &timer);
        MR_TEST_ASSERT_EQ_PTR(last_param, (void *)0x1111);
    }

    ret = mr_timer_stop(&timer);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    ret = mr_timer_del(&timer);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
}
MR_TEST_EXPORT(timer, periodic, test_timer_periodic);
#endif /* defined(MR_USE_TIMER) */
#endif /* defined(MR_USE_TEST) */
