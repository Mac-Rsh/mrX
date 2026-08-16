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

static mr_wait_event_t last_event;
static const void *last_data;
static void *last_param;
static int trigger_count;
static int trigger_mask;

static mr_waitable_t *test_recursive_able;
static mr_waitable_t *test_attach_able;
static mr_waiter_t *test_cancel_target;
static mr_waiter_t *test_attach_waiter;
static int test_recursive_count[3];
static mr_bool_t test_recursive;

static const mr_clazz_t test_waitable_clazz
    = MR_CLAZZ_INIT("test_waitable", MR_NULL, MR_CLAZZES_STATIK(waitable));
static const mr_clazz_t test_waiter_clazz
    = MR_CLAZZ_INIT("test_waiter", MR_NULL, MR_CLAZZES_STATIK(waiter));

static void test_waiter_cb(mr_waiter_t *waiter, void *param,
                           mr_wait_event_t event, const void *data) {
    MR_IGNORE(waiter);
    trigger_count++;
    last_event = event;
    last_param = param;
    last_data = data;
}

static void test_remove_cb(mr_waiter_t *waiter, void *param,
                           mr_wait_event_t event, const void *data) {
    int id = (int)(mr_uintptr_t)param;

    MR_IGNORE(event);
    MR_IGNORE(data);
    trigger_count++;
    trigger_mask |= 1 << id;

    if (id == 0) {
        mr_waiter_cancel(waiter);
        mr_waiter_cancel(test_cancel_target);
    }
}

static void test_recursive_cb(mr_waiter_t *waiter, void *param,
                              mr_wait_event_t event, const void *data) {
    int id = (int)(mr_uintptr_t)param;

    MR_IGNORE(waiter);
    MR_IGNORE(data);
    test_recursive_count[id]++;

    if ((id == 0) && !test_recursive) {
        test_recursive = MR_TRUE;
        mr_waitable_raise(test_recursive_able, event, data);
        test_recursive = MR_FALSE;
    } else if ((id == 0) && test_recursive) {
        mr_waiter_cancel(test_cancel_target);
    }
}

static void test_attach_cb(mr_waiter_t *waiter, void *param,
                           mr_wait_event_t event, const void *data) {
    int id = (int)(mr_uintptr_t)param;

    MR_IGNORE(waiter);
    MR_IGNORE(event);
    MR_IGNORE(data);
    trigger_count++;
    trigger_mask |= 1 << id;

    if ((id == 0) && !MR_WAITER_IS_WAITING(test_attach_waiter)) {
        mr_waiter_wait(test_attach_waiter, test_attach_able);
    }
}

static void test_waiter_delete_cb(mr_waiter_t *waiter, void *param,
                                  mr_wait_event_t event, const void *data) {
    MR_IGNORE(param);
    MR_IGNORE(event);
    MR_IGNORE(data);
    trigger_count++;
    mr_waiter_del(waiter);
}

static void test_waitable_delete_cb(mr_waiter_t *waiter, void *param,
                                    mr_wait_event_t event, const void *data) {
    MR_IGNORE(waiter);
    MR_IGNORE(event);
    MR_IGNORE(data);
    trigger_count++;
    mr_waitable_del(param);
}

/* Helpers */
static void test_wait_reset(void) {
    last_event = 0;
    last_data = MR_NULL;
    last_param = MR_NULL;
    trigger_count = 0;
    trigger_mask = 0;
    test_recursive_able = MR_NULL;
    test_attach_able = MR_NULL;
    test_cancel_target = MR_NULL;
    test_attach_waiter = MR_NULL;
    test_recursive_count[0] = 0;
    test_recursive_count[1] = 0;
    test_recursive_count[2] = 0;
    test_recursive = MR_FALSE;
}

/* Tests */
static void test_wait_waitable_derive_init(void) {
    static mr_waitable_t waitable
        = MR_WAITABLE_DERIVE_INIT(&waitable, &test_waitable_clazz);

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

    ret = mr_waitable_init(&waitable);
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

    waitable = mr_waitable_create();
    MR_TEST_ASSERT_NE_PTR(waitable, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(MR_OBJECT_IS_INITED(waitable), MR_TRUE);

    ret = mr_waitable_del(waitable);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
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

static void test_wait_waiter_is_waiting(void) {
    mr_waitable_t waitable;
    mr_waiter_t waiter;
    mr_err_t ret;

    ret = mr_waitable_init(&waitable);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_waiter_init(&waiter, test_waiter_cb, MR_NULL, MR_WAIT_IN);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(MR_WAITER_IS_WAITING(&waiter), MR_FALSE);

    ret = mr_waiter_wait(&waiter, &waitable);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(MR_WAITER_IS_WAITING(&waiter), MR_TRUE);

    ret = mr_waiter_wait(&waiter, &waitable);
    MR_TEST_ASSERT_EQ_INT(ret, -MR_EBUSY);

    ret = mr_waiter_cancel(&waiter);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(MR_WAITER_IS_WAITING(&waiter), MR_FALSE);

    mr_waiter_del(&waiter);
    mr_waitable_del(&waitable);
}
MR_TEST_EXPORT(wait, waiter_is_waiting, test_wait_waiter_is_waiting);

static void test_wait_raise_basic(void) {
    static const int data = 0x12345678;
    mr_waitable_t waitable;
    mr_waiter_t waiter;
    mr_err_t ret;

    test_wait_reset();

    ret = mr_waitable_init(&waitable);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_waiter_init(&waiter, test_waiter_cb, (void *)0x12345678,
                         MR_WAIT_IN);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_waiter_wait(&waiter, &waitable);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    ret = mr_waitable_raise(&waitable, MR_WAIT_IN, &data);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(trigger_count, 1);
    MR_TEST_ASSERT_EQ_UINT(last_event, MR_WAIT_IN);
    MR_TEST_ASSERT_EQ_PTR(last_param, (void *)0x12345678);
    MR_TEST_ASSERT_EQ_PTR(last_data, &data);
    MR_TEST_ASSERT_EQ_INT(mr_list_is_empty(&waitable.cursors), MR_TRUE);

    mr_waiter_del(&waiter);
    mr_waitable_del(&waitable);
}
MR_TEST_EXPORT(wait, raise_basic, test_wait_raise_basic);

static void test_wait_multiple_waiters(void) {
    mr_waitable_t waitable;
    mr_waiter_t waiter_in;
    mr_waiter_t waiter_out;
    mr_waiter_t waiter_err;
    mr_err_t ret;

    test_wait_reset();

    ret = mr_waitable_init(&waitable);
    ret |= mr_waiter_init(&waiter_in, test_waiter_cb, MR_NULL, MR_WAIT_IN);
    ret |= mr_waiter_init(&waiter_out, test_waiter_cb, MR_NULL, MR_WAIT_OUT);
    ret |= mr_waiter_init(&waiter_err, test_waiter_cb, MR_NULL, MR_WAIT_ERR);
    ret |= mr_waiter_wait(&waiter_in, &waitable);
    ret |= mr_waiter_wait(&waiter_out, &waitable);
    ret |= mr_waiter_wait(&waiter_err, &waitable);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    ret = mr_waitable_raise(&waitable, MR_WAIT_IN | MR_WAIT_OUT, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(trigger_count, 2);

    mr_waiter_del(&waiter_in);
    mr_waiter_del(&waiter_out);
    mr_waiter_del(&waiter_err);
    mr_waitable_del(&waitable);
}
MR_TEST_EXPORT(wait, multiple_waiters, test_wait_multiple_waiters);

static void test_wait_edge_level(void) {
    mr_waitable_t waitable;
    mr_waiter_t waiter_edge;
    mr_waiter_t waiter_level;
    mr_err_t ret;

    test_wait_reset();

    ret = mr_waitable_init(&waitable);
    ret |= mr_waiter_init(&waiter_edge, test_waiter_cb, MR_NULL,
                          MR_WAIT_IN | MR_WAIT_EDGE);
    ret |= mr_waiter_init(&waiter_level, test_waiter_cb, MR_NULL, MR_WAIT_IN);
    ret |= mr_waiter_wait(&waiter_edge, &waitable);
    ret |= mr_waiter_wait(&waiter_level, &waitable);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    mr_waitable_raise(&waitable, MR_WAIT_IN, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(trigger_count, 2);

    mr_waitable_raise(&waitable, MR_WAIT_IN, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(trigger_count, 3);

    mr_waitable_raise(&waitable, 0, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(trigger_count, 3);

    mr_waitable_raise(&waitable, MR_WAIT_IN, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(trigger_count, 5);
    MR_TEST_ASSERT_EQ_UINT(waiter_edge.last, MR_WAIT_IN);

    mr_waiter_del(&waiter_edge);
    mr_waiter_del(&waiter_level);
    mr_waitable_del(&waitable);
}
MR_TEST_EXPORT(wait, edge_level, test_wait_edge_level);

static void test_wait_attach_during_handle(void) {
    mr_waitable_t waitable;
    mr_waiter_t waiter_a;
    mr_waiter_t waiter_b;
    mr_waiter_t waiter_new;
    mr_err_t ret;

    test_wait_reset();

    ret = mr_waitable_init(&waitable);
    ret |= mr_waiter_init(&waiter_a, test_attach_cb, (void *)0, MR_WAIT_IN);
    ret |= mr_waiter_init(&waiter_b, test_attach_cb, (void *)1, MR_WAIT_IN);
    ret |= mr_waiter_init(&waiter_new, test_attach_cb, (void *)2, MR_WAIT_IN);
    ret |= mr_waiter_wait(&waiter_a, &waitable);
    ret |= mr_waiter_wait(&waiter_b, &waitable);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    test_attach_able = &waitable;
    test_attach_waiter = &waiter_new;
    ret = mr_waitable_raise(&waitable, MR_WAIT_IN, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(trigger_count, 3);
    MR_TEST_ASSERT_EQ_INT(trigger_mask, (1 << 0) | (1 << 1) | (1 << 2));
    MR_TEST_ASSERT_EQ_INT(MR_WAITER_IS_WAITING(&waiter_new), MR_TRUE);

    trigger_count = 0;
    trigger_mask = 0;
    ret = mr_waitable_raise(&waitable, MR_WAIT_IN, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(trigger_count, 3);
    MR_TEST_ASSERT_EQ_INT(trigger_mask, (1 << 0) | (1 << 1) | (1 << 2));

    mr_waiter_del(&waiter_a);
    mr_waiter_del(&waiter_b);
    mr_waiter_del(&waiter_new);
    mr_waitable_del(&waitable);
}
MR_TEST_EXPORT(wait, attach_during_handle, test_wait_attach_during_handle);

static void test_wait_cursor_removal(void) {
    mr_waitable_t waitable;
    mr_waiter_t waiter_a;
    mr_waiter_t waiter_b;
    mr_waiter_t waiter_c;
    mr_err_t ret;

    test_wait_reset();

    ret = mr_waitable_init(&waitable);
    ret |= mr_waiter_init(&waiter_a, test_remove_cb, (void *)0, MR_WAIT_IN);
    ret |= mr_waiter_init(&waiter_b, test_remove_cb, (void *)1, MR_WAIT_IN);
    ret |= mr_waiter_init(&waiter_c, test_remove_cb, (void *)2, MR_WAIT_IN);
    ret |= mr_waiter_wait(&waiter_a, &waitable);
    ret |= mr_waiter_wait(&waiter_b, &waitable);
    ret |= mr_waiter_wait(&waiter_c, &waitable);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    test_cancel_target = &waiter_b;
    ret = mr_waitable_raise(&waitable, MR_WAIT_IN, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(trigger_count, 2);
    MR_TEST_ASSERT_EQ_INT(trigger_mask, (1 << 0) | (1 << 2));
    MR_TEST_ASSERT_EQ_INT(MR_WAITER_IS_WAITING(&waiter_a), MR_FALSE);
    MR_TEST_ASSERT_EQ_INT(MR_WAITER_IS_WAITING(&waiter_b), MR_FALSE);
    MR_TEST_ASSERT_EQ_INT(MR_WAITER_IS_WAITING(&waiter_c), MR_TRUE);

    mr_waiter_del(&waiter_a);
    mr_waiter_del(&waiter_b);
    mr_waiter_del(&waiter_c);
    mr_waitable_del(&waitable);
}
MR_TEST_EXPORT(wait, cursor_removal, test_wait_cursor_removal);

static void test_wait_recursive_cursor_removal(void) {
    mr_waitable_t waitable;
    mr_waiter_t waiter_a;
    mr_waiter_t waiter_b;
    mr_waiter_t waiter_c;
    mr_err_t ret;

    test_wait_reset();

    ret = mr_waitable_init(&waitable);
    ret |= mr_waiter_init(&waiter_a, test_recursive_cb, (void *)0, MR_WAIT_IN);
    ret |= mr_waiter_init(&waiter_b, test_recursive_cb, (void *)1, MR_WAIT_IN);
    ret |= mr_waiter_init(&waiter_c, test_recursive_cb, (void *)2, MR_WAIT_IN);
    ret |= mr_waiter_wait(&waiter_a, &waitable);
    ret |= mr_waiter_wait(&waiter_b, &waitable);
    ret |= mr_waiter_wait(&waiter_c, &waitable);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    test_recursive_able = &waitable;
    test_cancel_target = &waiter_b;
    ret = mr_waitable_raise(&waitable, MR_WAIT_IN, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(test_recursive_count[0], 2);
    MR_TEST_ASSERT_EQ_INT(test_recursive_count[1], 0);
    MR_TEST_ASSERT_EQ_INT(test_recursive_count[2], 2);

    mr_waiter_del(&waiter_a);
    mr_waiter_del(&waiter_b);
    mr_waiter_del(&waiter_c);
    mr_waitable_del(&waitable);
}
MR_TEST_EXPORT(wait, recursive_cursor_removal,
               test_wait_recursive_cursor_removal);

static void test_wait_delete_during_handle(void) {
    mr_waitable_t waitable;
    mr_waiter_t waitable_waiter;
    mr_waiter_t *dynamic_waiter;
    mr_err_t ret;

    test_wait_reset();

    ret = mr_waitable_init(&waitable);
    dynamic_waiter
        = mr_waiter_create(test_waiter_delete_cb, MR_NULL, MR_WAIT_IN);
    MR_TEST_ASSERT_NE_PTR(dynamic_waiter, MR_NULL);
    ret |= mr_waiter_wait(dynamic_waiter, &waitable);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    ret = mr_waitable_raise(&waitable, MR_WAIT_IN, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(trigger_count, 1);
    MR_TEST_ASSERT_EQ_INT(mr_list_is_empty(&waitable.list), MR_TRUE);

    ret = mr_waiter_init(&waitable_waiter, test_waitable_delete_cb, &waitable,
                         MR_WAIT_IN);
    ret |= mr_waiter_wait(&waitable_waiter, &waitable);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    ret = mr_waitable_raise(&waitable, MR_WAIT_IN, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(trigger_count, 2);
    MR_TEST_ASSERT_EQ_INT(MR_OBJECT_IS_INITED(&waitable), MR_FALSE);
    MR_TEST_ASSERT_EQ_INT(MR_WAITER_IS_WAITING(&waitable_waiter), MR_FALSE);

    mr_waiter_del(&waitable_waiter);
}
MR_TEST_EXPORT(wait, delete_during_handle, test_wait_delete_during_handle);

static void test_wait_event_get_set(void) {
    mr_waitable_t waitable;
    mr_waiter_t waiter;
    mr_err_t ret;

    test_wait_reset();

    ret = mr_waitable_init(&waitable);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_waiter_init(&waiter, test_waiter_cb, MR_NULL, MR_WAIT_IN);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_waiter_wait(&waiter, &waitable);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    MR_TEST_ASSERT_EQ_UINT(mr_waiter_event(&waiter), MR_WAIT_IN);

    ret = mr_waiter_event_set(&waiter, MR_WAIT_OUT);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(MR_WAITER_IS_WAITING(&waiter), MR_FALSE);
    ret = mr_waiter_wait(&waiter, &waitable);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    mr_waitable_raise(&waitable, MR_WAIT_IN, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(trigger_count, 0);
    mr_waitable_raise(&waitable, MR_WAIT_OUT, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(trigger_count, 1);

    mr_waiter_del(&waiter);
    mr_waitable_del(&waitable);
}
MR_TEST_EXPORT(wait, event_get_set, test_wait_event_get_set);

static void test_wait_cancel_all_busy(void) {
    mr_waitable_t waitable;
    mr_waiter_t waiter_a;
    mr_waiter_t waiter_b;
    mr_err_t ret;

    test_wait_reset();
    ret = mr_waitable_init(&waitable);
    ret |= mr_waiter_init(&waiter_a, test_waiter_cb, MR_NULL, MR_WAIT_IN);
    ret |= mr_waiter_init(&waiter_b, test_waiter_cb, MR_NULL, MR_WAIT_OUT);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    ret = mr_waiter_wait(&waiter_a, &waitable);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_waiter_wait(&waiter_a, &waitable);
    MR_TEST_ASSERT_EQ_INT(ret, -MR_EBUSY);
    ret = mr_waiter_wait(&waiter_b, &waitable);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_waitable_cancel_all(&waitable);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(MR_WAITER_IS_WAITING(&waiter_a), MR_FALSE);
    MR_TEST_ASSERT_EQ_INT(MR_WAITER_IS_WAITING(&waiter_b), MR_FALSE);

    ret = mr_waiter_cancel(&waiter_a);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_waiter_cancel(&waiter_b);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    mr_waiter_del(&waiter_a);
    mr_waiter_del(&waiter_b);
    mr_waitable_del(&waitable);
}
MR_TEST_EXPORT(wait, cancel_all_busy, test_wait_cancel_all_busy);

static void test_wait_event_mask_and_edge_data(void) {
    mr_waitable_t waitable;
    mr_waiter_t waiter;
    mr_uint32_t data = 0x12345678U;
    mr_err_t ret;

    test_wait_reset();
    ret = mr_waitable_init(&waitable);
    ret |= mr_waiter_init(&waiter, test_waiter_cb, (void *)0x42,
                          MR_WAIT_EDGE | MR_WAIT_IN);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_waiter_wait(&waiter, &waitable);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    ret = mr_waitable_raise(&waitable, MR_WAIT_IN | 0x00000005U, &data);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(trigger_count, 1);
    MR_TEST_ASSERT_EQ_UINT(last_event, MR_WAIT_IN | 0x00000005U);
    MR_TEST_ASSERT_EQ_PTR(last_data, &data);
    MR_TEST_ASSERT_EQ_PTR(last_param, (void *)0x42);

    ret = mr_waitable_raise(&waitable, MR_WAIT_IN, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(trigger_count, 1);
    ret = mr_waitable_raise(&waitable, 0, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(trigger_count, 1);
    ret = mr_waitable_raise(&waitable, MR_WAIT_IN, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(trigger_count, 2);

    mr_waiter_del(&waiter);
    mr_waitable_del(&waitable);
}
MR_TEST_EXPORT(wait, event_mask_edge_data, test_wait_event_mask_and_edge_data);

static void test_wait_dynamic_reference_lifecycle(void) {
    mr_waitable_t *waitable;
    mr_waiter_t *waiter;
    mr_err_t ret;

    test_wait_reset();
    waitable = mr_waitable_create();
    waiter = mr_waiter_create(test_waiter_cb, MR_NULL, MR_WAIT_IN);
    MR_TEST_ASSERT_NE_PTR(waitable, MR_NULL);
    MR_TEST_ASSERT_NE_PTR(waiter, MR_NULL);

    MR_TEST_ASSERT_NE_PTR(mr_waitable_get(waitable), MR_NULL);
    MR_TEST_ASSERT_EQ_INT(mr_waitable_put(waitable), MR_FALSE);
    MR_TEST_ASSERT_NE_PTR(mr_waiter_get(waiter), MR_NULL);
    MR_TEST_ASSERT_EQ_INT(mr_waiter_put(waiter), MR_FALSE);

    ret = mr_waiter_wait(waiter, waitable);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_waitable_del(waitable);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(MR_WAITER_IS_WAITING(waiter), MR_FALSE);
    ret = mr_waiter_del(waiter);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
}
MR_TEST_EXPORT(wait, dynamic_reference_lifecycle,
               test_wait_dynamic_reference_lifecycle);
#endif /* defined(MR_USE_WAIT) */
#endif /* defined(MR_USE_TEST) */
