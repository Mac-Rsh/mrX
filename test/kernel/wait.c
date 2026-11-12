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

typedef struct test_wait_ctx {
    mr_wait_event_t last_event;
    const void *last_data;
    void *last_param;
    int trigger_count;
    int trigger_mask;
    int recursive_count[3];
    mr_bool_t recursive;
    mr_waitable_t *recursive_able;
    mr_waitable_t *attach_able;
    mr_waitable_t *delete_able;
    mr_waiter_t *cancel_target;
    mr_waiter_t *attach_waiter;
} test_wait_ctx_t;

typedef struct test_wait_arg {
    test_wait_ctx_t *ctx;
    int id;
} test_wait_arg_t;

static const mr_clazz_t test_waitable_clazz
    = MR_CLAZZ_INIT("test_waitable", MR_NULL, MR_CLAZZES_STATIK(waitable));
static const mr_clazz_t test_waiter_clazz
    = MR_CLAZZ_INIT("test_waiter", MR_NULL, MR_CLAZZES_STATIK(waiter));

static void test_waiter_cb(mr_waiter_t *waiter, void *param,
                           mr_wait_event_t event, const void *data) {
    test_wait_ctx_t *ctx = param;

    MR_IGNORE(waiter);
    if (ctx == MR_NULL) {
        return;
    }
    ctx->trigger_count++;
    ctx->last_event = event;
    ctx->last_param = param;
    ctx->last_data = data;
}

static void test_remove_cb(mr_waiter_t *waiter, void *param,
                           mr_wait_event_t event, const void *data) {
    test_wait_arg_t *arg = param;
    test_wait_ctx_t *ctx = arg->ctx;

    MR_IGNORE(event);
    MR_IGNORE(data);
    ctx->trigger_count++;
    ctx->trigger_mask |= 1 << arg->id;

    if (arg->id == 0) {
        mr_waiter_cancel(waiter);
        mr_waiter_cancel(ctx->cancel_target);
    }
}

static void test_recursive_cb(mr_waiter_t *waiter, void *param,
                              mr_wait_event_t event, const void *data) {
    test_wait_arg_t *arg = param;
    test_wait_ctx_t *ctx = arg->ctx;

    MR_IGNORE(waiter);
    MR_IGNORE(data);
    ctx->recursive_count[arg->id]++;

    if ((arg->id == 0) && !ctx->recursive) {
        ctx->recursive = MR_TRUE;
        mr_waitable_raise(ctx->recursive_able, event, data);
        ctx->recursive = MR_FALSE;
    } else if ((arg->id == 0) && ctx->recursive) {
        mr_waiter_cancel(ctx->cancel_target);
    }
}

static void test_attach_cb(mr_waiter_t *waiter, void *param,
                           mr_wait_event_t event, const void *data) {
    test_wait_arg_t *arg = param;
    test_wait_ctx_t *ctx = arg->ctx;

    MR_IGNORE(waiter);
    MR_IGNORE(event);
    MR_IGNORE(data);
    ctx->trigger_count++;
    ctx->trigger_mask |= 1 << arg->id;

    if ((arg->id == 0) && !MR_WAITER_IS_WAITING(ctx->attach_waiter)) {
        mr_waiter_wait(ctx->attach_waiter, ctx->attach_able);
    }
}

static void test_waiter_delete_cb(mr_waiter_t *waiter, void *param,
                                  mr_wait_event_t event, const void *data) {
    test_wait_ctx_t *ctx = param;

    MR_IGNORE(event);
    MR_IGNORE(data);
    ctx->trigger_count++;
    mr_waiter_del(waiter);
}

static void test_waitable_delete_cb(mr_waiter_t *waiter, void *param,
                                    mr_wait_event_t event, const void *data) {
    test_wait_ctx_t *ctx = param;

    MR_IGNORE(waiter);
    MR_IGNORE(event);
    MR_IGNORE(data);
    ctx->trigger_count++;
    mr_waitable_del(ctx->delete_able);
}

MR_TEST(wait, waitable_derive_init) {
    static mr_waitable_t waitable
        = MR_WAITABLE_DERIVE_INIT(&waitable, &test_waitable_clazz);

    MR_TEST_ASSERT_EQ_PTR(MR_OBJECT_CLAZZ(&waitable), &test_waitable_clazz);
}

MR_TEST(wait, waiter_derive_init) {
    static mr_waiter_t waiter = MR_WAITER_DERIVE_INIT(
        &waiter, &test_waiter_clazz, test_waiter_cb, MR_NULL, MR_WAIT_IN);

    MR_TEST_ASSERT_EQ_PTR(MR_OBJECT_CLAZZ(&waiter), &test_waiter_clazz);
}

MR_TEST(wait, waitable_init_del) {
    mr_waitable_t waitable;
    mr_err_t ret;

    ret = mr_waitable_init(&waitable);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(MR_OBJECT_IS_INITED(&waitable), MR_TRUE);

    ret = mr_waitable_del(&waitable);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(MR_OBJECT_IS_INITED(&waitable), MR_FALSE);
}

MR_TEST(wait, waitable_create_del) {
    mr_waitable_t *waitable;
    mr_err_t ret;

    waitable = mr_waitable_create();
    MR_TEST_ASSERT_NE_PTR(waitable, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(MR_OBJECT_IS_INITED(waitable), MR_TRUE);

    ret = mr_waitable_del(waitable);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
}

MR_TEST(wait, waiter_init_del) {
    mr_waiter_t waiter;
    mr_err_t ret;

    ret = mr_waiter_init(&waiter, test_waiter_cb, MR_NULL, MR_WAIT_IN);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(MR_OBJECT_IS_INITED(&waiter), MR_TRUE);

    ret = mr_waiter_del(&waiter);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(MR_OBJECT_IS_INITED(&waiter), MR_FALSE);
}

MR_TEST(wait, waiter_create_del) {
    mr_waiter_t *waiter;
    mr_err_t ret;

    waiter = mr_waiter_create(test_waiter_cb, MR_NULL, MR_WAIT_IN);
    MR_TEST_ASSERT_NE_PTR(waiter, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(MR_OBJECT_IS_INITED(waiter), MR_TRUE);

    ret = mr_waiter_del(waiter);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
}

MR_TEST(wait, waiter_is_waiting) {
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

MR_TEST(wait, raise_basic) {
    static const int data = 0x12345678;
    mr_waitable_t waitable;
    mr_waiter_t waiter;
    mr_err_t ret;

    test_wait_ctx_t ctx = {0};

    ret = mr_waitable_init(&waitable);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_waiter_init(&waiter, test_waiter_cb, &ctx, MR_WAIT_IN);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_waiter_wait(&waiter, &waitable);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    ret = mr_waitable_raise(&waitable, MR_WAIT_IN, &data);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(ctx.trigger_count, 1);
    MR_TEST_ASSERT_EQ_UINT(ctx.last_event, MR_WAIT_IN);
    MR_TEST_ASSERT_EQ_PTR(ctx.last_param, &ctx);
    MR_TEST_ASSERT_EQ_PTR(ctx.last_data, &data);
    MR_TEST_ASSERT_EQ_INT(mr_list_is_empty(&waitable.cursors), MR_TRUE);

    mr_waiter_del(&waiter);
    mr_waitable_del(&waitable);
}

MR_TEST(wait, multiple_waiters) {
    mr_waitable_t waitable;
    mr_waiter_t waiter_in;
    mr_waiter_t waiter_out;
    mr_waiter_t waiter_err;
    mr_err_t ret;

    test_wait_ctx_t ctx = {0};

    ret = mr_waitable_init(&waitable);
    ret |= mr_waiter_init(&waiter_in, test_waiter_cb, &ctx, MR_WAIT_IN);
    ret |= mr_waiter_init(&waiter_out, test_waiter_cb, &ctx, MR_WAIT_OUT);
    ret |= mr_waiter_init(&waiter_err, test_waiter_cb, &ctx, MR_WAIT_ERR);
    ret |= mr_waiter_wait(&waiter_in, &waitable);
    ret |= mr_waiter_wait(&waiter_out, &waitable);
    ret |= mr_waiter_wait(&waiter_err, &waitable);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    ret = mr_waitable_raise(&waitable, MR_WAIT_IN | MR_WAIT_OUT, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(ctx.trigger_count, 2);

    mr_waiter_del(&waiter_in);
    mr_waiter_del(&waiter_out);
    mr_waiter_del(&waiter_err);
    mr_waitable_del(&waitable);
}

MR_TEST(wait, edge_level) {
    mr_waitable_t waitable;
    mr_waiter_t waiter_edge;
    mr_waiter_t waiter_level;
    mr_err_t ret;

    test_wait_ctx_t ctx = {0};

    ret = mr_waitable_init(&waitable);
    ret |= mr_waiter_init(&waiter_edge, test_waiter_cb, &ctx,
                          MR_WAIT_IN | MR_WAIT_EDGE);
    ret |= mr_waiter_init(&waiter_level, test_waiter_cb, &ctx, MR_WAIT_IN);
    ret |= mr_waiter_wait(&waiter_edge, &waitable);
    ret |= mr_waiter_wait(&waiter_level, &waitable);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    mr_waitable_raise(&waitable, MR_WAIT_IN, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ctx.trigger_count, 2);

    mr_waitable_raise(&waitable, MR_WAIT_IN, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ctx.trigger_count, 3);

    mr_waitable_raise(&waitable, 0, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ctx.trigger_count, 3);

    mr_waitable_raise(&waitable, MR_WAIT_IN, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ctx.trigger_count, 5);
    MR_TEST_ASSERT_EQ_UINT(waiter_edge.last, MR_WAIT_IN);

    mr_waiter_del(&waiter_edge);
    mr_waiter_del(&waiter_level);
    mr_waitable_del(&waitable);
}

MR_TEST(wait, attach_during_handle) {
    mr_waitable_t waitable;
    mr_waiter_t waiter_a;
    mr_waiter_t waiter_b;
    mr_waiter_t waiter_new;
    test_wait_arg_t arg_a;
    test_wait_arg_t arg_b;
    test_wait_arg_t arg_new;
    mr_err_t ret;
    test_wait_ctx_t ctx = {0};

    arg_a.ctx = &ctx;
    arg_a.id = 0;
    arg_b.ctx = &ctx;
    arg_b.id = 1;
    arg_new.ctx = &ctx;
    arg_new.id = 2;

    ret = mr_waitable_init(&waitable);
    ret |= mr_waiter_init(&waiter_a, test_attach_cb, &arg_a, MR_WAIT_IN);
    ret |= mr_waiter_init(&waiter_b, test_attach_cb, &arg_b, MR_WAIT_IN);
    ret |= mr_waiter_init(&waiter_new, test_attach_cb, &arg_new, MR_WAIT_IN);
    ret |= mr_waiter_wait(&waiter_a, &waitable);
    ret |= mr_waiter_wait(&waiter_b, &waitable);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    ctx.attach_able = &waitable;
    ctx.attach_waiter = &waiter_new;
    ret = mr_waitable_raise(&waitable, MR_WAIT_IN, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(ctx.trigger_count, 3);
    MR_TEST_ASSERT_EQ_INT(ctx.trigger_mask, (1 << 0) | (1 << 1) | (1 << 2));
    MR_TEST_ASSERT_EQ_INT(MR_WAITER_IS_WAITING(&waiter_new), MR_TRUE);

    ctx.trigger_count = 0;
    ctx.trigger_mask = 0;
    ret = mr_waitable_raise(&waitable, MR_WAIT_IN, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(ctx.trigger_count, 3);
    MR_TEST_ASSERT_EQ_INT(ctx.trigger_mask, (1 << 0) | (1 << 1) | (1 << 2));

    mr_waiter_del(&waiter_a);
    mr_waiter_del(&waiter_b);
    mr_waiter_del(&waiter_new);
    mr_waitable_del(&waitable);
}

MR_TEST(wait, cursor_removal) {
    mr_waitable_t waitable;
    mr_waiter_t waiter_a;
    mr_waiter_t waiter_b;
    mr_waiter_t waiter_c;
    test_wait_arg_t arg_a;
    test_wait_arg_t arg_b;
    test_wait_arg_t arg_c;
    mr_err_t ret;
    test_wait_ctx_t ctx = {0};

    arg_a.ctx = &ctx;
    arg_a.id = 0;
    arg_b.ctx = &ctx;
    arg_b.id = 1;
    arg_c.ctx = &ctx;
    arg_c.id = 2;

    ret = mr_waitable_init(&waitable);
    ret |= mr_waiter_init(&waiter_a, test_remove_cb, &arg_a, MR_WAIT_IN);
    ret |= mr_waiter_init(&waiter_b, test_remove_cb, &arg_b, MR_WAIT_IN);
    ret |= mr_waiter_init(&waiter_c, test_remove_cb, &arg_c, MR_WAIT_IN);
    ret |= mr_waiter_wait(&waiter_a, &waitable);
    ret |= mr_waiter_wait(&waiter_b, &waitable);
    ret |= mr_waiter_wait(&waiter_c, &waitable);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    ctx.cancel_target = &waiter_b;
    ret = mr_waitable_raise(&waitable, MR_WAIT_IN, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(ctx.trigger_count, 2);
    MR_TEST_ASSERT_EQ_INT(ctx.trigger_mask, (1 << 0) | (1 << 2));
    MR_TEST_ASSERT_EQ_INT(MR_WAITER_IS_WAITING(&waiter_a), MR_FALSE);
    MR_TEST_ASSERT_EQ_INT(MR_WAITER_IS_WAITING(&waiter_b), MR_FALSE);
    MR_TEST_ASSERT_EQ_INT(MR_WAITER_IS_WAITING(&waiter_c), MR_TRUE);

    mr_waiter_del(&waiter_a);
    mr_waiter_del(&waiter_b);
    mr_waiter_del(&waiter_c);
    mr_waitable_del(&waitable);
}

MR_TEST(wait, recursive_cursor_removal) {
    mr_waitable_t waitable;
    mr_waiter_t waiter_a;
    mr_waiter_t waiter_b;
    mr_waiter_t waiter_c;
    test_wait_arg_t arg_a;
    test_wait_arg_t arg_b;
    test_wait_arg_t arg_c;
    mr_err_t ret;
    test_wait_ctx_t ctx = {0};

    arg_a.ctx = &ctx;
    arg_a.id = 0;
    arg_b.ctx = &ctx;
    arg_b.id = 1;
    arg_c.ctx = &ctx;
    arg_c.id = 2;

    ret = mr_waitable_init(&waitable);
    ret |= mr_waiter_init(&waiter_a, test_recursive_cb, &arg_a, MR_WAIT_IN);
    ret |= mr_waiter_init(&waiter_b, test_recursive_cb, &arg_b, MR_WAIT_IN);
    ret |= mr_waiter_init(&waiter_c, test_recursive_cb, &arg_c, MR_WAIT_IN);
    ret |= mr_waiter_wait(&waiter_a, &waitable);
    ret |= mr_waiter_wait(&waiter_b, &waitable);
    ret |= mr_waiter_wait(&waiter_c, &waitable);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    ctx.recursive_able = &waitable;
    ctx.cancel_target = &waiter_b;
    ret = mr_waitable_raise(&waitable, MR_WAIT_IN, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(ctx.recursive_count[0], 2);
    MR_TEST_ASSERT_EQ_INT(ctx.recursive_count[1], 0);
    MR_TEST_ASSERT_EQ_INT(ctx.recursive_count[2], 2);

    mr_waiter_del(&waiter_a);
    mr_waiter_del(&waiter_b);
    mr_waiter_del(&waiter_c);
    mr_waitable_del(&waitable);
}

MR_TEST(wait, delete_during_handle) {
    mr_waitable_t waitable;
    mr_waiter_t waitable_waiter;
    mr_waiter_t *dynamic_waiter;
    mr_err_t ret;

    test_wait_ctx_t ctx = {0};

    ret = mr_waitable_init(&waitable);
    dynamic_waiter = mr_waiter_create(test_waiter_delete_cb, &ctx, MR_WAIT_IN);
    MR_TEST_ASSERT_NE_PTR(dynamic_waiter, MR_NULL);
    ret |= mr_waiter_wait(dynamic_waiter, &waitable);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    ret = mr_waitable_raise(&waitable, MR_WAIT_IN, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(ctx.trigger_count, 1);
    MR_TEST_ASSERT_EQ_INT(mr_list_is_empty(&waitable.list), MR_TRUE);

    ctx.delete_able = &waitable;
    ret = mr_waiter_init(&waitable_waiter, test_waitable_delete_cb, &ctx,
                         MR_WAIT_IN);
    ret |= mr_waiter_wait(&waitable_waiter, &waitable);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    ret = mr_waitable_raise(&waitable, MR_WAIT_IN, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(ctx.trigger_count, 2);
    MR_TEST_ASSERT_EQ_INT(MR_OBJECT_IS_INITED(&waitable), MR_FALSE);
    MR_TEST_ASSERT_EQ_INT(MR_WAITER_IS_WAITING(&waitable_waiter), MR_FALSE);

    mr_waiter_del(&waitable_waiter);
}

MR_TEST(wait, event_get_set) {
    mr_waitable_t waitable;
    mr_waiter_t waiter;
    mr_err_t ret;

    test_wait_ctx_t ctx = {0};

    ret = mr_waitable_init(&waitable);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_waiter_init(&waiter, test_waiter_cb, &ctx, MR_WAIT_IN);
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
    MR_TEST_ASSERT_EQ_INT(ctx.trigger_count, 0);
    mr_waitable_raise(&waitable, MR_WAIT_OUT, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ctx.trigger_count, 1);

    mr_waiter_del(&waiter);
    mr_waitable_del(&waitable);
}

MR_TEST(wait, cancel_all_busy) {
    mr_waitable_t waitable;
    mr_waiter_t waiter_a;
    mr_waiter_t waiter_b;
    mr_err_t ret;

    test_wait_ctx_t ctx = {0};
    ret = mr_waitable_init(&waitable);
    ret |= mr_waiter_init(&waiter_a, test_waiter_cb, &ctx, MR_WAIT_IN);
    ret |= mr_waiter_init(&waiter_b, test_waiter_cb, &ctx, MR_WAIT_OUT);
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

MR_TEST(wait, event_mask_edge_data) {
    mr_waitable_t waitable;
    mr_waiter_t waiter;
    mr_uint32_t data = 0x12345678U;
    mr_err_t ret;

    test_wait_ctx_t ctx = {0};
    ret = mr_waitable_init(&waitable);
    ret |= mr_waiter_init(&waiter, test_waiter_cb, &ctx,
                          MR_WAIT_EDGE | MR_WAIT_IN);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_waiter_wait(&waiter, &waitable);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    ret = mr_waitable_raise(&waitable, MR_WAIT_IN | 0x00000005U, &data);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(ctx.trigger_count, 1);
    MR_TEST_ASSERT_EQ_UINT(ctx.last_event, MR_WAIT_IN | 0x00000005U);
    MR_TEST_ASSERT_EQ_PTR(ctx.last_data, &data);
    MR_TEST_ASSERT_EQ_PTR(ctx.last_param, &ctx);

    ret = mr_waitable_raise(&waitable, MR_WAIT_IN, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(ctx.trigger_count, 1);
    ret = mr_waitable_raise(&waitable, 0, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(ctx.trigger_count, 1);
    ret = mr_waitable_raise(&waitable, MR_WAIT_IN, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(ctx.trigger_count, 2);

    mr_waiter_del(&waiter);
    mr_waitable_del(&waitable);
}

MR_TEST(wait, dynamic_reference_lifecycle) {
    mr_waitable_t *waitable;
    mr_waiter_t *waiter;
    mr_err_t ret;

    test_wait_ctx_t ctx = {0};
    waitable = mr_waitable_create();
    waiter = mr_waiter_create(test_waiter_cb, &ctx, MR_WAIT_IN);
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

MR_TEST(wait, raise_no_waiters) {
    mr_waitable_t waitable;
    mr_err_t ret;

    ret = mr_waitable_init(&waitable);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_waitable_raise(&waitable, MR_WAIT_IN, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_waitable_cancel_all(&waitable);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_waitable_del(&waitable);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
}

MR_TEST(wait, cancel_idle) {
    mr_waiter_t waiter;
    mr_err_t ret;

    ret = mr_waiter_init(&waiter, test_waiter_cb, MR_NULL, MR_WAIT_IN);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(MR_WAITER_IS_WAITING(&waiter), MR_FALSE);
    ret = mr_waiter_cancel(&waiter);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(MR_WAITER_IS_WAITING(&waiter), MR_FALSE);
    ret = mr_waiter_del(&waiter);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
}

MR_TEST(wait, unmatched_event) {
    mr_waitable_t waitable;
    mr_waiter_t waiter;
    mr_err_t ret;
    test_wait_ctx_t ctx = {0};

    ret = mr_waitable_init(&waitable);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_waiter_init(&waiter, test_waiter_cb, &ctx, MR_WAIT_IN);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_waiter_wait(&waiter, &waitable);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    ret = mr_waitable_raise(&waitable, MR_WAIT_OUT, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(ctx.trigger_count, 0);
    MR_TEST_ASSERT_EQ_INT(MR_WAITER_IS_WAITING(&waiter), MR_TRUE);

    ret = mr_waiter_cancel(&waiter);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    mr_waiter_del(&waiter);
    mr_waitable_del(&waitable);
}

MR_TEST(wait, wait_after_cancel) {
    static const int data = 7;
    mr_waitable_t waitable;
    mr_waiter_t waiter;
    mr_err_t ret;
    test_wait_ctx_t ctx = {0};

    ret = mr_waitable_init(&waitable);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_waiter_init(&waiter, test_waiter_cb, &ctx, MR_WAIT_IN);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_waiter_wait(&waiter, &waitable);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_waiter_cancel(&waiter);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    ret = mr_waiter_wait(&waiter, &waitable);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_waitable_raise(&waitable, MR_WAIT_IN, &data);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(ctx.trigger_count, 1);
    MR_TEST_ASSERT_EQ_PTR(ctx.last_data, &data);

    mr_waiter_del(&waiter);
    mr_waitable_del(&waitable);
}

#endif /* defined(MR_USE_WAIT) */
#endif /* defined(MR_USE_TEST) */
