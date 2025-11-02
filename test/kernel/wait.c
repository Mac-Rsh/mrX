/**
 * @copyright (c) 2024-2025, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mr-X/test/mr_test.h>
#if defined(MR_USE_TEST)
#if defined(MR_USE_WAIT)
#include <mr-X/kernel/mr_wait.h>

static void *last_param = MR_NULL;
static mr_uint32_t last_event = 0;
static int trig_status = 0;

static void test_waiter_cb(mr_waiter_t *waiter, void *param,
                           mr_uint32_t event) {
    MR_UNUSED(waiter);
    MR_UNUSED(param);
    trig_status++;
    last_event = event;
    last_param = param;
}

/* -------------------------------------------------------------------------- */
/* Test: Waitable init / del                                                  */
static void test_waitable_init_del(void) {
    mr_waitable_t waitable;
    mr_err_t ret;

    ret = mr_waitable_init(&waitable, (mr_waitable_check_t *)1, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(MR_OBJECT_IS_INITED(&waitable), MR_TRUE);

    ret = mr_waitable_del(&waitable);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(MR_OBJECT_IS_INITED(&waitable), MR_FALSE);
}
MR_TEST_EXPORT(wait, waitable_init_del, test_waitable_init_del);

/* -------------------------------------------------------------------------- */
/* Test: Waitable create / del                                                */
static void test_waitable_create_del(void) {
    mr_waitable_t *waitable;
    mr_err_t ret;

    waitable = mr_waitable_create((mr_waitable_check_t *)1, MR_NULL);
    MR_TEST_ASSERT_NE_PTR(waitable, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(MR_OBJECT_IS_INITED(waitable), MR_TRUE);

    ret = mr_waitable_del(waitable);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(MR_OBJECT_IS_INITED(waitable), MR_FALSE);
}
MR_TEST_EXPORT(wait, waitable_create_del, test_waitable_create_del);

/* -------------------------------------------------------------------------- */
/* Test: Waiter init / del                                                    */
static void test_waiter_init_del(void) {
    mr_waiter_t waiter;
    mr_err_t ret;

    ret = mr_waiter_init(&waiter, test_waiter_cb, MR_NULL, MR_WAIT_IN);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(MR_OBJECT_IS_INITED(&waiter), MR_TRUE);

    ret = mr_waiter_del(&waiter);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(MR_OBJECT_IS_INITED(&waiter), MR_FALSE);
}
MR_TEST_EXPORT(wait, waiter_init_del, test_waiter_init_del);

/* -------------------------------------------------------------------------- */
/* Test: Waiter create / del                                                  */
static void test_waiter_create_del(void) {
    mr_waiter_t *waiter;
    mr_err_t ret;

    waiter = mr_waiter_create(test_waiter_cb, MR_NULL, MR_WAIT_IN);
    MR_TEST_ASSERT_NE_PTR(waiter, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(MR_OBJECT_IS_INITED(waiter), MR_TRUE);

    ret = mr_waiter_del(waiter);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
}
MR_TEST_EXPORT(wait, waiter_create_del, test_waiter_create_del);

static mr_uint32_t test_check_basic(mr_waitable_t *w, void *p) {
    MR_UNUSED(w);
    MR_UNUSED(p);
    return MR_WAIT_IN;
}

/* -------------------------------------------------------------------------- */
/* Test: MR_WAITER_IS_WAITING                                                */
static void test_waiter_is_waiting(void) {
    mr_waitable_t waitable;
    mr_waiter_t waiter;
    mr_err_t ret;

    mr_waitable_init(&waitable, test_check_basic, MR_NULL);
    mr_waiter_init(&waiter, test_waiter_cb, MR_NULL, MR_WAIT_IN);
    MR_TEST_ASSERT_EQ_INT(MR_WAITER_IS_WAITING(&waiter), MR_FALSE);

    ret = mr_waiter_wait(&waiter, &waitable);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(MR_WAITER_IS_WAITING(&waiter), MR_TRUE);

    ret = mr_waiter_wait(&waiter, &waitable);
    MR_TEST_ASSERT_EQ_INT(ret, -MR_EBUSY);

    mr_waiter_cancel(&waiter);
    MR_TEST_ASSERT_EQ_INT(MR_WAITER_IS_WAITING(&waiter), MR_FALSE);

    mr_waiter_del(&waiter);
    mr_waitable_del(&waitable);
}
MR_TEST_EXPORT(wait, waiter_is_waiting, test_waiter_is_waiting);

/* -------------------------------------------------------------------------- */
/* Test: Basic completion and callback                                        */
static void test_waitable_complete_basic(void) {
    mr_waitable_t waitable;
    mr_waiter_t waiter;
    mr_err_t ret;

    mr_waitable_init(&waitable, test_check_basic, MR_NULL);
    mr_waiter_init(&waiter, test_waiter_cb, (void *)0x12345678, MR_WAIT_IN);

    trig_status = 0;
    ret = mr_waiter_wait(&waiter, &waitable);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(trig_status, 1);
    MR_TEST_ASSERT_EQ_INT(last_event, MR_WAIT_IN);
    MR_TEST_ASSERT_EQ_PTR(last_param, (void *)0x12345678);

    ret = mr_waitable_complete(&waitable);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(trig_status, 2);
    MR_TEST_ASSERT_EQ_INT(last_event, MR_WAIT_IN);
    MR_TEST_ASSERT_EQ_PTR(last_param, (void *)0x12345678);

    mr_waiter_del(&waiter);
    mr_waitable_del(&waitable);
}
MR_TEST_EXPORT(wait, complete_basic, test_waitable_complete_basic);

static void test_waitable_edge_level(void) {
    mr_waiter_t waiter_edge, waiter_level;
    mr_waitable_t waitable;
    mr_err_t ret;

    mr_waitable_init(&waitable, test_check_basic, MR_NULL);
    mr_waiter_init(&waiter_edge, test_waiter_cb, MR_NULL,
                   MR_WAIT_IN | MR_WAIT_EDGE);

    trig_status = 0;
    ret = mr_waiter_wait(&waiter_edge, &waitable);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(trig_status, 0);

    mr_waiter_init(&waiter_level, test_waiter_cb, MR_NULL, MR_WAIT_IN);

    trig_status = 0;
    ret = mr_waiter_wait(&waiter_level, &waitable);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(trig_status, 1);

    trig_status = 0;
    mr_waitable_complete(&waitable);
    MR_TEST_ASSERT_EQ_INT(trig_status, 2);

    mr_waitable_complete(&waitable);
    MR_TEST_ASSERT_EQ_INT(trig_status, 3);

    mr_waitable_del(&waitable);
    mr_waiter_del(&waiter_edge);
    mr_waiter_del(&waiter_level);
}
MR_TEST_EXPORT(wait, edge_level, test_waitable_edge_level);

/* -------------------------------------------------------------------------- */
/* Test: Multiple waiters on same waitable                                    */
static mr_uint32_t test_check_multi(mr_waitable_t *w, void *p) {
    MR_UNUSED(w);
    MR_UNUSED(p);
    return MR_WAIT_OUT;
}

static void test_multiple_waiters(void) {
    mr_waitable_t waitable;
    mr_waiter_t w1, w2, w3;
    mr_err_t ret;

    mr_waitable_init(&waitable, test_check_multi, MR_NULL);
    mr_waitable_complete(&waitable);

    ret = mr_waiter_init(&w1, test_waiter_cb, (void *)1, MR_WAIT_OUT);
    ret |= mr_waiter_wait(&w1, &waitable);

    ret |= mr_waiter_init(&w2, test_waiter_cb, (void *)2, MR_WAIT_OUT);
    ret |= mr_waiter_wait(&w2, &waitable);

    ret |= mr_waiter_init(&w3, test_waiter_cb, (void *)3, MR_WAIT_ERR);
    ret |= mr_waiter_wait(&w3, &waitable);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    trig_status = 0;
    mr_waitable_complete(&waitable);
    MR_TEST_ASSERT_EQ_INT(trig_status, 2);

    mr_waiter_del(&w1);
    mr_waiter_del(&w2);
    mr_waiter_del(&w3);
    mr_waitable_del(&waitable);
}
MR_TEST_EXPORT(wait, multiple_waiters, test_multiple_waiters);

/* -------------------------------------------------------------------------- */
/* Test: Race / generation skip                                               */
static mr_uint32_t test_check_race(mr_waitable_t *w, void *p) {
    static mr_uint32_t gen = 0;

    MR_UNUSED(p);
    if ((gen++) == 0)
    {
        mr_waitable_complete(w);
    }
    return MR_WAIT_IN;
}

static void test_generation_skip(void) {
    mr_waitable_t waitable;
    mr_waiter_t waiter;
    mr_err_t ret;

    mr_waitable_init(&waitable, test_check_race, MR_NULL);
    mr_waiter_init(&waiter, test_waiter_cb, MR_NULL, MR_WAIT_IN);

    trig_status = 0;
    ret = mr_waiter_wait(&waiter, &waitable);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(trig_status, 1);

    mr_waitable_complete(&waitable);
    MR_TEST_ASSERT_EQ_INT(trig_status, 2);

    mr_waiter_del(&waiter);
    mr_waitable_del(&waitable);
}
MR_TEST_EXPORT(wait, generation_skip, test_generation_skip);

static void test_event_get_set(void) {
    mr_waitable_t waitable;
    mr_waiter_t waiter;
    mr_err_t ret;

    mr_waitable_init(&waitable, test_check_race, MR_NULL);
    mr_waiter_init(&waiter, test_waiter_cb, MR_NULL, MR_WAIT_IN);

    MR_TEST_ASSERT_EQ_UINT(mr_waiter_event(&waiter), MR_WAIT_IN);

    trig_status = 0;
    ret = mr_waiter_wait(&waiter, &waitable);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(trig_status, 1);

    mr_waiter_event_set(&waiter, MR_WAIT_OUT);
    mr_waitable_complete(&waitable);
    MR_TEST_ASSERT_EQ_INT(trig_status, 1);

    mr_waiter_del(&waiter);
    mr_waitable_del(&waitable);
}
MR_TEST_EXPORT(wait, event_get_set, test_event_get_set);
#endif /* defined(MR_USE_WAIT) */
#endif /* defined(MR_USE_TEST) */
