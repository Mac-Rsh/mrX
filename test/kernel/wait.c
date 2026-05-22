/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/test/mr_test.h>
#if defined(MR_USE_TEST)
#if defined(MR_USE_WAIT)
#include <mrX/kernel/mr_wait.h>

static mr_uint64_t last_event = 0;
static void *last_param = MR_NULL;
static int trig_status = 0;
static mr_uint64_t test_wait_race_gen = 0;
static const mr_clazz_t test_waitable_clazz
    = MR_CLAZZ_INIT("test_waitable", MR_NULL, MR_CLAZZES_STATIK(waitable));
static const mr_clazz_t test_waiter_clazz
    = MR_CLAZZ_INIT("test_waiter", MR_NULL, MR_CLAZZES_STATIK(waiter));

static void test_waiter_cb(mr_waiter_t *waiter, void *param,
                           mr_uint64_t event) {
    MR_IGNORE(waiter);
    MR_IGNORE(param);
    trig_status++;
    last_event = event;
    last_param = param;
}

/* Helpers */
static void test_wait_reset(void) {
    last_event = 0;
    last_param = MR_NULL;
    trig_status = 0;
    test_wait_race_gen = 0;
}

/* Tests */
static void test_wait_waitable_derive_init(void) {
    static mr_waitable_t waitable = MR_WAITABLE_DERIVE_INIT(
        &waitable, &test_waitable_clazz, (mr_waitable_check_t *)1, MR_NULL);

    MR_TEST_ASSERT_EQ_PTR(MR_OBJECT_CLAZZ(&waitable), &test_waitable_clazz);
}
MR_TEST_EXPORT(wait, waitable_derive_init, test_wait_waitable_derive_init);

static void test_wait_waiter_derive_init(void) {
    static mr_waiter_t waiter = MR_WAITER_DERIVE_INIT(
        &waiter, &test_waiter_clazz, test_waiter_cb, MR_NULL, MR_WAIT_IN);

    MR_TEST_ASSERT_EQ_PTR(MR_OBJECT_CLAZZ(&waiter), &test_waiter_clazz);
}
MR_TEST_EXPORT(wait, waiter_derive_init, test_wait_waiter_derive_init);

static void test_wait_waitable_init_del(void) {
    mr_waitable_t waitable;
    mr_err_t ret;

    ret = mr_waitable_init(&waitable, (mr_waitable_check_t *)1, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(MR_OBJECT_IS_INITED(&waitable), MR_TRUE);

    ret = mr_waitable_del(&waitable);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(MR_OBJECT_IS_INITED(&waitable), MR_FALSE);
}
MR_TEST_EXPORT(wait, waitable_init_del, test_wait_waitable_init_del);

static void test_wait_waitable_create_del(void) {
    mr_waitable_t *waitable;
    mr_err_t ret;

    waitable = mr_waitable_create((mr_waitable_check_t *)1, MR_NULL);
    MR_TEST_ASSERT_NE_PTR(waitable, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(MR_OBJECT_IS_INITED(waitable), MR_TRUE);

    ret = mr_waitable_del(waitable);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(MR_OBJECT_IS_INITED(waitable), MR_FALSE);
}
MR_TEST_EXPORT(wait, waitable_create_del, test_wait_waitable_create_del);

static void test_wait_waiter_init_del(void) {
    mr_waiter_t waiter;
    mr_err_t ret;

    ret = mr_waiter_init(&waiter, test_waiter_cb, MR_NULL, MR_WAIT_IN);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(MR_OBJECT_IS_INITED(&waiter), MR_TRUE);

    ret = mr_waiter_del(&waiter);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(MR_OBJECT_IS_INITED(&waiter), MR_FALSE);
}
MR_TEST_EXPORT(wait, waiter_init_del, test_wait_waiter_init_del);

static void test_wait_waiter_create_del(void) {
    mr_waiter_t *waiter;
    mr_err_t ret;

    waiter = mr_waiter_create(test_waiter_cb, MR_NULL, MR_WAIT_IN);
    MR_TEST_ASSERT_NE_PTR(waiter, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(MR_OBJECT_IS_INITED(waiter), MR_TRUE);

    ret = mr_waiter_del(waiter);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
}
MR_TEST_EXPORT(wait, waiter_create_del, test_wait_waiter_create_del);

static mr_uint64_t test_check_basic(mr_waitable_t *w, void *p) {
    MR_IGNORE(w);
    MR_IGNORE(p);
    return MR_WAIT_IN;
}

static void test_wait_waiter_is_waiting(void) {
    mr_waitable_t waitable;
    mr_waiter_t waiter;
    mr_err_t ret;

    test_wait_reset();

    ret = mr_waitable_init(&waitable, test_check_basic, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_waiter_init(&waiter, test_waiter_cb, MR_NULL, MR_WAIT_IN);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
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
MR_TEST_EXPORT(wait, waiter_is_waiting, test_wait_waiter_is_waiting);

static void test_wait_complete_basic(void) {
    mr_waitable_t waitable;
    mr_waiter_t waiter;
    mr_err_t ret;

    test_wait_reset();

    ret = mr_waitable_init(&waitable, test_check_basic, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_waiter_init(&waiter, test_waiter_cb, (void *)0x12345678,
                         MR_WAIT_IN);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

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
MR_TEST_EXPORT(wait, complete_basic, test_wait_complete_basic);

static void test_wait_edge_level(void) {
    mr_waiter_t waiter_edge;
    mr_waiter_t waiter_level;
    mr_waitable_t waitable;
    mr_err_t ret;

    test_wait_reset();

    ret = mr_waitable_init(&waitable, test_check_basic, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_waiter_init(&waiter_edge, test_waiter_cb, MR_NULL,
                         MR_WAIT_IN | MR_WAIT_EDGE);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    ret = mr_waiter_wait(&waiter_edge, &waitable);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(trig_status, 0);

    ret = mr_waiter_init(&waiter_level, test_waiter_cb, MR_NULL, MR_WAIT_IN);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    trig_status = 0;
    ret = mr_waiter_wait(&waiter_level, &waitable);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(trig_status, 1);

    trig_status = 0;
    mr_waitable_complete(&waitable);
    MR_TEST_ASSERT_EQ_INT(trig_status, 2);

    mr_waitable_complete(&waitable);
    MR_TEST_ASSERT_EQ_INT(trig_status, 3);

    mr_waitable_raise(&waitable, MR_WAIT_IN);
    MR_TEST_ASSERT_EQ_INT(trig_status, 4);

    mr_waitable_del(&waitable);
    mr_waiter_del(&waiter_edge);
    mr_waiter_del(&waiter_level);
}
MR_TEST_EXPORT(wait, edge_level, test_wait_edge_level);

static mr_uint64_t test_check_multi(mr_waitable_t *w, void *p) {
    MR_IGNORE(w);
    MR_IGNORE(p);
    return MR_WAIT_OUT;
}

static void test_wait_multiple_waiters(void) {
    mr_waitable_t waitable;
    mr_waiter_t w1;
    mr_waiter_t w2;
    mr_waiter_t w3;
    mr_err_t ret;

    test_wait_reset();

    ret = mr_waitable_init(&waitable, test_check_multi, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
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
MR_TEST_EXPORT(wait, multiple_waiters, test_wait_multiple_waiters);

static mr_uint64_t test_check_race(mr_waitable_t *w, void *p) {
    MR_IGNORE(p);
    if ((test_wait_race_gen++) == 0) {
        mr_waitable_complete(w);
    }
    return MR_WAIT_IN;
}

static void test_wait_generation_skip(void) {
    mr_waitable_t waitable;
    mr_waiter_t waiter;
    mr_err_t ret;

    test_wait_reset();

    ret = mr_waitable_init(&waitable, test_check_race, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_waiter_init(&waiter, test_waiter_cb, MR_NULL, MR_WAIT_IN);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    ret = mr_waiter_wait(&waiter, &waitable);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(trig_status, 1);

    mr_waitable_complete(&waitable);
    MR_TEST_ASSERT_EQ_INT(trig_status, 2);

    mr_waiter_del(&waiter);
    mr_waitable_del(&waitable);
}
MR_TEST_EXPORT(wait, generation_skip, test_wait_generation_skip);

static void test_wait_event_get_set(void) {
    mr_waitable_t waitable;
    mr_waiter_t waiter;
    mr_err_t ret;

    test_wait_reset();

    ret = mr_waitable_init(&waitable, test_check_race, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_waiter_init(&waiter, test_waiter_cb, MR_NULL, MR_WAIT_IN);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    MR_TEST_ASSERT_EQ_UINT(mr_waiter_event(&waiter), MR_WAIT_IN);

    ret = mr_waiter_wait(&waiter, &waitable);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(trig_status, 1);

    mr_waiter_event_set(&waiter, MR_WAIT_OUT);
    mr_waitable_complete(&waitable);
    MR_TEST_ASSERT_EQ_INT(trig_status, 1);

    mr_waiter_del(&waiter);
    mr_waitable_del(&waitable);
}
MR_TEST_EXPORT(wait, event_get_set, test_wait_event_get_set);
#endif /* defined(MR_USE_WAIT) */
#endif /* defined(MR_USE_TEST) */
