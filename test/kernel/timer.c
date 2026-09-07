/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/test/mr_test.h>
#if defined(MR_USE_TEST)
#if defined(MR_USE_TIMER)
#include <mrX/kernel/mr_timer.h>
#include <mrX/kernel/mr_clock.h>

typedef struct test_timer_ctx {
    mr_timer_t *last_timer;
} test_timer_ctx_t;

static const mr_clazz_t test_timer_clazz
    = MR_CLAZZ_INIT("test_timer", MR_NULL, MR_CLAZZES_STATIK(timer));

static void test_timer_entry(mr_timer_t *timer, void *param) {
    test_timer_ctx_t *ctx = param;

    if (ctx == MR_NULL) {
        return;
    }
    ctx->last_timer = timer;
}

static void test_timer_rearm_entry(mr_timer_t *timer, void *param) {
    int *count = param;

    (*count)++;
    if (*count == 1) {
        mr_timer_start(timer);
    }
}

static void test_timer_stop_entry(mr_timer_t *timer, void *param) {
    int *count = param;

    (*count)++;
    mr_timer_stop(timer);
}

/* Tests */
MR_TEST(timer, derive_init) {
    static mr_timer_t timer = MR_TIMER_DERIVE_INIT(
        &timer, &test_timer_clazz, test_timer_entry, MR_NULL, 1);

    MR_TEST_ASSERT_EQ_PTR(MR_OBJECT_CLAZZ(&timer), &test_timer_clazz);
}

MR_TEST(timer, init_del) {
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

MR_TEST(timer, create_del) {
    mr_timer_t *timer;
    mr_err_t ret;

    timer = mr_timer_create(test_timer_entry, (void *)0x5678, 2000);
    MR_TEST_ASSERT_NE_PTR(timer, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(MR_OBJECT_IS_INITED(timer), MR_TRUE);
    MR_TEST_ASSERT_EQ_UINT(timer->itick, 2000);

    ret = mr_timer_del(timer);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
}

MR_TEST(timer, start_stop) {
    mr_timer_t timer;
    mr_timer_t timer1;
    test_timer_ctx_t ctx = {0};
    mr_err_t ret;
    int timer_inited;
    int timer1_inited;

    timer_inited = MR_FALSE;
    timer1_inited = MR_FALSE;

    ret = mr_timer_init(&timer, test_timer_entry, &ctx, 2);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    timer_inited = MR_TRUE;

    ret = mr_timer_start(&timer);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret != 0) {
        goto _exit;
    }

    ret = mr_timer_init(&timer1, test_timer_entry, &ctx, 2);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret != 0) {
        goto _exit;
    }
    timer1_inited = MR_TRUE;
    ret = mr_timer_start(&timer1);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    ret = mr_timer_del(&timer1);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    timer1_inited = MR_FALSE;

    ret = mr_timer_init(&timer1, test_timer_entry, &ctx, 1);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret != 0) {
        goto _exit;
    }
    timer1_inited = MR_TRUE;
    ret = mr_timer_start(&timer1);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    ret = mr_timer_del(&timer1);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    timer1_inited = MR_FALSE;

    ctx.last_timer = MR_NULL;
    mr_clock_increase(2);
    MR_TEST_EXPECT_EQ_PTR(ctx.last_timer, &timer);

    ret = mr_timer_stop(&timer);
    MR_TEST_EXPECT_EQ_INT(ret, 0);

_exit:
    if (timer1_inited == MR_TRUE) {
        ret = mr_timer_del(&timer1);
        MR_TEST_EXPECT_EQ_INT(ret, 0);
    }
    if (timer_inited == MR_TRUE) {
        ret = mr_timer_del(&timer);
        MR_TEST_EXPECT_EQ_INT(ret, 0);
    }
}

MR_TEST(timer, tick_get_set) {
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

MR_TEST(timer, periodic) {
    mr_timer_t timer;
    test_timer_ctx_t ctx = {0};
    mr_err_t ret;
    int timer_inited;
    int i;

    timer_inited = MR_FALSE;

    ret = mr_timer_init(&timer, test_timer_entry, &ctx, 1 | MR_TIMER_PERIODIC);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    timer_inited = MR_TRUE;

    ret = mr_timer_start(&timer);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret != 0) {
        goto _exit;
    }

    for (i = 0; i < 10; i++) {
        ctx.last_timer = MR_NULL;
        mr_clock_increase(1);
        MR_TEST_EXPECT_EQ_PTR(ctx.last_timer, &timer);
    }

    ret = mr_timer_stop(&timer);
    MR_TEST_EXPECT_EQ_INT(ret, 0);

_exit:
    if (timer_inited == MR_TRUE) {
        ret = mr_timer_del(&timer);
        MR_TEST_EXPECT_EQ_INT(ret, 0);
    }
}

MR_TEST(timer, oneshot_does_not_repeat) {
    mr_timer_t timer;
    test_timer_ctx_t ctx = {0};
    mr_err_t ret;

    ret = mr_timer_init(&timer, test_timer_entry, &ctx, 1);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    ret = mr_timer_start(&timer);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    MR_TEST_EXPECT_EQ_INT(MR_TIMER_IS_TIMING(&timer), MR_TRUE);

    ctx.last_timer = MR_NULL;
    mr_clock_increase(1);
    MR_TEST_EXPECT_EQ_PTR(ctx.last_timer, &timer);
    MR_TEST_EXPECT_EQ_INT(MR_TIMER_IS_TIMING(&timer), MR_FALSE);

    ctx.last_timer = MR_NULL;
    mr_clock_increase(1);
    MR_TEST_EXPECT_EQ_PTR(ctx.last_timer, MR_NULL);

    ret = mr_timer_del(&timer);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
}

MR_TEST(timer, restart) {
    mr_timer_t timer;
    test_timer_ctx_t ctx = {0};
    mr_err_t ret;

    ret = mr_timer_init(&timer, test_timer_entry, &ctx, 5);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_timer_start(&timer);
    MR_TEST_EXPECT_EQ_INT(ret, 0);

    mr_clock_increase(2);
    ret = mr_timer_start(&timer);
    MR_TEST_EXPECT_EQ_INT(ret, 0);

    ctx.last_timer = MR_NULL;
    mr_clock_increase(2);
    MR_TEST_EXPECT_EQ_PTR(ctx.last_timer, MR_NULL);

    ctx.last_timer = MR_NULL;
    mr_clock_increase(3);
    MR_TEST_EXPECT_EQ_PTR(ctx.last_timer, &timer);

    ret = mr_timer_del(&timer);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
}

MR_TEST(timer, oneshot_rearm_from_entry) {
    mr_timer_t timer;
    int count = 0;
    mr_err_t ret;

    ret = mr_timer_init(&timer, test_timer_rearm_entry, &count, 2);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(mr_timer_start(&timer), 0);

    mr_clock_increase(2);
    MR_TEST_ASSERT_EQ_INT(count, 1);
    MR_TEST_ASSERT_EQ_INT(MR_TIMER_IS_TIMING(&timer), MR_TRUE);
    mr_clock_increase(1);
    MR_TEST_ASSERT_EQ_INT(count, 1);
    mr_clock_increase(1);
    MR_TEST_ASSERT_EQ_INT(count, 2);
    MR_TEST_ASSERT_EQ_INT(MR_TIMER_IS_TIMING(&timer), MR_FALSE);

    ret = mr_timer_del(&timer);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
}

MR_TEST(timer, periodic_stop_from_entry) {
    mr_timer_t timer;
    int count = 0;
    mr_err_t ret;

    ret = mr_timer_init(&timer, test_timer_stop_entry, &count,
                        1 | MR_TIMER_PERIODIC);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(mr_timer_start(&timer), 0);

    mr_clock_increase(1);
    MR_TEST_ASSERT_EQ_INT(count, 1);
    MR_TEST_ASSERT_EQ_INT(MR_TIMER_IS_TIMING(&timer), MR_FALSE);
    mr_clock_increase(3);
    MR_TEST_ASSERT_EQ_INT(count, 1);

    ret = mr_timer_del(&timer);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
}

MR_TEST(timer, stop_idle_and_tick_set) {
    mr_timer_t timer;
    test_timer_ctx_t ctx = {0};
    mr_err_t ret;

    ret = mr_timer_init(&timer, test_timer_entry, &ctx, 3);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_timer_stop(&timer);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(MR_TIMER_IS_TIMING(&timer), MR_FALSE);

    ret = mr_timer_start(&timer);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    ret = mr_timer_tick_set(&timer, 7);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_UINT(mr_timer_tick(&timer), 7);
    MR_TEST_ASSERT_EQ_INT(MR_TIMER_IS_TIMING(&timer), MR_FALSE);

    ctx.last_timer = MR_NULL;
    mr_clock_increase(7);
    MR_TEST_EXPECT_EQ_PTR(ctx.last_timer, MR_NULL);

    ret = mr_timer_del(&timer);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
}

MR_TEST(timer, get_put) {
    mr_timer_t *timer;
    mr_err_t ret;

    timer = mr_timer_create(test_timer_entry, MR_NULL, 4);
    MR_TEST_ASSERT_NE_PTR(timer, MR_NULL);
    MR_TEST_ASSERT_NE_PTR(mr_timer_get(timer), MR_NULL);
    MR_TEST_ASSERT_EQ_INT(mr_timer_put(timer), MR_FALSE);
    ret = mr_timer_del(timer);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
}

#endif /* defined(MR_USE_TIMER) */
#endif /* defined(MR_USE_TEST) */
