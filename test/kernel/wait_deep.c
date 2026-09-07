/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2026-08-30        MacRsh        Deep test version
 */

#include <mrX/test/mr_test.h>
#if defined(MR_USE_TEST)
#if defined(MR_USE_WAIT)
#include <mrX/kernel/mr_wait.h>

/* Helpers */
typedef struct deep_wait_ctx {
    int id_order[12];
    int order_n;
    int id_count[5];
    int wait_ret;
    int cancel_all_ret;
    int event_set_ret;
    mr_wait_event_t last_event;
    const void *last_data;
    int raise_guard;
    mr_waitable_t *able;
    mr_waitable_t *able2;
    mr_waiter_t *waiters[5];
    int cancel_target;
    int event_set_target;
    int event_set_event;
    int rewait_self;
} deep_wait_ctx_t;

typedef struct deep_wait_arg {
    deep_wait_ctx_t *ctx;
    int id;
} deep_wait_arg_t;

static void deep_wait_record(deep_wait_ctx_t *ctx, int id) {
    if (ctx->order_n < 12) {
        ctx->id_order[ctx->order_n] = id;
    }
    ctx->order_n++;
    if ((id >= 0) && (id < 5)) {
        ctx->id_count[id]++;
    }
}

static void deep_wait_reset(deep_wait_ctx_t *ctx) {
    int i;

    ctx->order_n = 0;
    ctx->wait_ret = 0;
    ctx->cancel_all_ret = 0;
    ctx->event_set_ret = 0;
    ctx->last_event = 0;
    ctx->last_data = MR_NULL;
    ctx->raise_guard = 0;
    ctx->able = MR_NULL;
    ctx->able2 = MR_NULL;
    ctx->cancel_target = -1;
    ctx->event_set_target = -1;
    ctx->event_set_event = 0;
    ctx->rewait_self = -1;
    for (i = 0; i < 5; i++) {
        ctx->id_count[i] = 0;
        ctx->waiters[i] = MR_NULL;
        ctx->id_order[i] = -1;
    }
}

static void deep_wait_cb(mr_waiter_t *waiter, void *param,
                         mr_wait_event_t event, const void *data) {
    deep_wait_arg_t *arg = param;
    deep_wait_ctx_t *ctx = arg->ctx;

    deep_wait_record(ctx, arg->id);
    ctx->last_event = event;
    ctx->last_data = data;

    if ((arg->id == 0) && (ctx->cancel_target >= 0)) {
        mr_waiter_cancel(ctx->waiters[ctx->cancel_target]);
        ctx->cancel_target = -1;
    }
    if (ctx->event_set_target >= 0) {
        ctx->event_set_ret = (int)mr_waiter_event_set(
            ctx->waiters[ctx->event_set_target],
            (mr_wait_event_t)ctx->event_set_event);
        ctx->event_set_target = -1;
    }
    if ((ctx->rewait_self == arg->id) && (ctx->rewait_self >= 0)) {
        mr_waiter_cancel(waiter);
        ctx->wait_ret = (int)mr_waiter_wait(waiter, ctx->able);
    }
}

/* Callback that raises the same waitable once (guarded) */
static void deep_wait_nested_cb(mr_waiter_t *waiter, void *param,
                                mr_wait_event_t event, const void *data) {
    deep_wait_arg_t *arg = param;
    deep_wait_ctx_t *ctx = arg->ctx;

    MR_IGNORE(waiter);
    deep_wait_record(ctx, arg->id);
    ctx->last_event = event;
    if ((arg->id == 0) && (ctx->raise_guard == 0)) {
        ctx->raise_guard = 1;
        mr_waitable_raise(ctx->able, event, data);
    }
}

/* Callback that cancels all waiters of the waitable */
static void deep_wait_cancel_all_cb(mr_waiter_t *waiter, void *param,
                                    mr_wait_event_t event, const void *data) {
    deep_wait_arg_t *arg = param;
    deep_wait_ctx_t *ctx = arg->ctx;

    MR_IGNORE(waiter);
    deep_wait_record(ctx, arg->id);
    ctx->last_event = event;
    ctx->last_data = data;
    if (arg->id == 0) {
        ctx->cancel_all_ret = (int)mr_waitable_cancel_all(ctx->able);
    }
}

/* Tests */

/* Test: waiters are woken strictly in wait order */
MR_TEST(wait, deep_fifo_wake_order) {
    deep_wait_ctx_t ctx;
    deep_wait_arg_t args[4];
    mr_waitable_t able;
    mr_waiter_t waiters[4];
    mr_err_t ret;
    int i;

    deep_wait_reset(&ctx);
    ret = mr_waitable_init(&able);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    for (i = 0; i < 4; i++) {
        args[i].ctx = &ctx;
        args[i].id = i;
        ret = mr_waiter_init(&waiters[i], deep_wait_cb, &args[i], MR_WAIT_IN);
        MR_TEST_ASSERT_EQ_INT(ret, 0);
        ctx.waiters[i] = &waiters[i];
        ret = mr_waiter_wait(&waiters[i], &able);
        MR_TEST_ASSERT_EQ_INT(ret, 0);
    }

    ret = mr_waitable_raise(&able, MR_WAIT_IN, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(ctx.order_n, 4);
    MR_TEST_ASSERT_EQ_INT(ctx.id_order[0], 0);
    MR_TEST_ASSERT_EQ_INT(ctx.id_order[1], 1);
    MR_TEST_ASSERT_EQ_INT(ctx.id_order[2], 2);
    MR_TEST_ASSERT_EQ_INT(ctx.id_order[3], 3);

    for (i = 0; i < 4; i++) {
        mr_waiter_del(&waiters[i]);
    }
    mr_waitable_del(&able);
}

/* Test: a waiter requeued from its own callback is visited again at the
 * tail of the same raise */
MR_TEST(wait, deep_rewait_moves_to_tail) {
    deep_wait_ctx_t ctx;
    deep_wait_arg_t args[3];
    mr_waitable_t able;
    mr_waiter_t waiters[3];
    mr_err_t ret;
    int i;

    deep_wait_reset(&ctx);
    ret = mr_waitable_init(&able);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ctx.able = &able;
    ctx.rewait_self = 0;

    for (i = 0; i < 3; i++) {
        args[i].ctx = &ctx;
        args[i].id = i;
        ret = mr_waiter_init(&waiters[i], deep_wait_cb, &args[i], MR_WAIT_IN);
        MR_TEST_ASSERT_EQ_INT(ret, 0);
        ctx.waiters[i] = &waiters[i];
        ret = mr_waiter_wait(&waiters[i], &able);
        MR_TEST_ASSERT_EQ_INT(ret, 0);
    }

    ret = mr_waitable_raise(&able, MR_WAIT_IN, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(ctx.wait_ret, 0);
    MR_TEST_ASSERT_EQ_INT(ctx.order_n, 4);
    MR_TEST_ASSERT_EQ_INT(ctx.id_order[0], 0);
    MR_TEST_ASSERT_EQ_INT(ctx.id_order[1], 1);
    MR_TEST_ASSERT_EQ_INT(ctx.id_order[2], 2);
    MR_TEST_ASSERT_EQ_INT(ctx.id_order[3], 0);
    MR_TEST_ASSERT_EQ_INT(ctx.id_count[0], 2);
    MR_TEST_ASSERT_EQ_INT(ctx.id_count[1], 1);
    MR_TEST_ASSERT_EQ_INT(ctx.id_count[2], 1);

    /* The next raise starts from the new head order and repeats the
     * pattern for the requeued waiter */
    ret = mr_waitable_raise(&able, MR_WAIT_IN, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(ctx.order_n, 7);
    MR_TEST_ASSERT_EQ_INT(ctx.id_order[4], 1);
    MR_TEST_ASSERT_EQ_INT(ctx.id_order[5], 2);
    MR_TEST_ASSERT_EQ_INT(ctx.id_order[6], 0);
    MR_TEST_ASSERT_EQ_INT(ctx.id_count[0], 3);
    MR_TEST_ASSERT_EQ_INT(ctx.id_count[1], 2);
    MR_TEST_ASSERT_EQ_INT(ctx.id_count[2], 2);

    ctx.rewait_self = -1;
    for (i = 0; i < 3; i++) {
        mr_waiter_del(&waiters[i]);
    }
    mr_waitable_del(&able);
}

/* Test: cancelling all waiters from a callback stops the current raise */
MR_TEST(wait, deep_cancel_all_during_handle) {
    deep_wait_ctx_t ctx;
    deep_wait_arg_t args[3];
    mr_waitable_t able;
    mr_waiter_t waiters[3];
    mr_err_t ret;
    int i;

    deep_wait_reset(&ctx);
    ret = mr_waitable_init(&able);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ctx.able = &able;

    for (i = 0; i < 3; i++) {
        args[i].ctx = &ctx;
        args[i].id = i;
        ret = mr_waiter_init(&waiters[i], i == 0 ? deep_wait_cancel_all_cb
                                                 : deep_wait_cb,
                             &args[i], MR_WAIT_IN);
        MR_TEST_ASSERT_EQ_INT(ret, 0);
        ctx.waiters[i] = &waiters[i];
        ret = mr_waiter_wait(&waiters[i], &able);
        MR_TEST_ASSERT_EQ_INT(ret, 0);
    }

    ret = mr_waitable_raise(&able, MR_WAIT_IN, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(ctx.cancel_all_ret, 0);
    MR_TEST_ASSERT_EQ_INT(ctx.order_n, 1);
    MR_TEST_ASSERT_EQ_INT(ctx.id_order[0], 0);
    for (i = 0; i < 3; i++) {
        MR_TEST_ASSERT_EQ_INT(MR_WAITER_IS_WAITING(&waiters[i]), MR_FALSE);
    }

    for (i = 0; i < 3; i++) {
        mr_waiter_del(&waiters[i]);
    }
    mr_waitable_del(&able);
}

/* Test: an edge waiter masks the previous event out of the delivered event */
MR_TEST(wait, deep_edge_payload_masking) {
    static const int data = 0x5a5a5a5a;
    deep_wait_ctx_t ctx;
    deep_wait_arg_t arg;
    mr_waitable_t able;
    mr_waiter_t waiter;
    mr_err_t ret;

    deep_wait_reset(&ctx);
    ret = mr_waitable_init(&able);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    arg.ctx = &ctx;
    arg.id = 0;
    ret = mr_waiter_init(&waiter, deep_wait_cb, &arg,
                         MR_WAIT_IN | MR_WAIT_EDGE);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ctx.waiters[0] = &waiter;
    ret = mr_waiter_wait(&waiter, &able);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    /* An unrelated event only updates the edge state */
    ret = mr_waitable_raise(&able, MR_WAIT_OUT, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(ctx.order_n, 0);

    ret = mr_waitable_raise(&able, MR_WAIT_IN | MR_WAIT_OUT, &data);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(ctx.order_n, 1);
    MR_TEST_ASSERT_EQ_UINT(ctx.last_event, MR_WAIT_IN);
    MR_TEST_ASSERT_EQ_PTR(ctx.last_data, &data);

    mr_waiter_del(&waiter);
    mr_waitable_del(&able);
}

/* Test: the edge bit of a raised event is stripped before delivery */
MR_TEST(wait, deep_raise_edge_bit_stripped) {
    deep_wait_ctx_t ctx;
    deep_wait_arg_t args[2];
    mr_waitable_t able;
    mr_waiter_t edge_waiter;
    mr_waiter_t level_waiter;
    mr_err_t ret;

    deep_wait_reset(&ctx);
    ret = mr_waitable_init(&able);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    args[0].ctx = &ctx;
    args[0].id = 0;
    args[1].ctx = &ctx;
    args[1].id = 1;
    ret = mr_waiter_init(&edge_waiter, deep_wait_cb, &args[0],
                         MR_WAIT_IN | MR_WAIT_EDGE);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_waiter_init(&level_waiter, deep_wait_cb, &args[1], MR_WAIT_IN);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_waiter_wait(&edge_waiter, &able);
    ret |= mr_waiter_wait(&level_waiter, &able);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    ret = mr_waitable_raise(&able, MR_WAIT_EDGE | MR_WAIT_IN, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(ctx.order_n, 2);
    MR_TEST_ASSERT_EQ_UINT(ctx.last_event, MR_WAIT_IN);

    mr_waiter_del(&edge_waiter);
    mr_waiter_del(&level_waiter);
    mr_waitable_del(&able);
}

/* Test: waiting again clears the edge state of a waiter */
MR_TEST(wait, deep_wait_resets_edge_state) {
    deep_wait_ctx_t ctx;
    deep_wait_arg_t arg;
    mr_waitable_t able;
    mr_waiter_t waiter;
    mr_err_t ret;

    deep_wait_reset(&ctx);
    ret = mr_waitable_init(&able);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    arg.ctx = &ctx;
    arg.id = 0;
    ret = mr_waiter_init(&waiter, deep_wait_cb, &arg,
                         MR_WAIT_IN | MR_WAIT_EDGE);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ctx.waiters[0] = &waiter;
    ret = mr_waiter_wait(&waiter, &able);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    ret = mr_waitable_raise(&able, MR_WAIT_IN, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(ctx.order_n, 1);

    /* A repeated raise must not retrigger the edge waiter */
    ret = mr_waitable_raise(&able, MR_WAIT_IN, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(ctx.order_n, 1);

    /* Waiting again resets the edge state and retriggers */
    ret = mr_waiter_cancel(&waiter);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_waiter_wait(&waiter, &able);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_waitable_raise(&able, MR_WAIT_IN, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(ctx.order_n, 2);

    mr_waiter_del(&waiter);
    mr_waitable_del(&able);
}

/* Test: a multi bit event mask matches each bit independently */
MR_TEST(wait, deep_multi_bit_event_mask) {
    deep_wait_ctx_t ctx;
    deep_wait_arg_t arg;
    mr_waitable_t able;
    mr_waiter_t waiter;
    mr_err_t ret;

    deep_wait_reset(&ctx);
    ret = mr_waitable_init(&able);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    arg.ctx = &ctx;
    arg.id = 0;
    ret = mr_waiter_init(&waiter, deep_wait_cb, &arg,
                         MR_WAIT_IN | MR_WAIT_OUT);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ctx.waiters[0] = &waiter;
    ret = mr_waiter_wait(&waiter, &able);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    ret = mr_waitable_raise(&able, MR_WAIT_IN, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(ctx.order_n, 1);

    ret = mr_waitable_raise(&able, MR_WAIT_OUT, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(ctx.order_n, 2);

    ret = mr_waitable_raise(&able, MR_WAIT_ERR, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(ctx.order_n, 2);

    ret = mr_waitable_raise(&able, MR_WAIT_IN | MR_WAIT_ERR, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(ctx.order_n, 3);
    MR_TEST_ASSERT_EQ_UINT(ctx.last_event, MR_WAIT_IN | MR_WAIT_ERR);

    mr_waiter_del(&waiter);
    mr_waitable_del(&able);
}

/* Test: changing the event of an unvisited waiter from a callback removes
 * it from the current raise */
MR_TEST(wait, deep_event_set_during_handle) {
    deep_wait_ctx_t ctx;
    deep_wait_arg_t args[2];
    mr_waitable_t able;
    mr_waiter_t waiters[2];
    mr_err_t ret;

    deep_wait_reset(&ctx);
    ret = mr_waitable_init(&able);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    args[0].ctx = &ctx;
    args[0].id = 0;
    args[1].ctx = &ctx;
    args[1].id = 1;
    ret = mr_waiter_init(&waiters[0], deep_wait_cb, &args[0], MR_WAIT_IN);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_waiter_init(&waiters[1], deep_wait_cb, &args[1], MR_WAIT_IN);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ctx.waiters[0] = &waiters[0];
    ctx.waiters[1] = &waiters[1];
    ret = mr_waiter_wait(&waiters[0], &able);
    ret |= mr_waiter_wait(&waiters[1], &able);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    ctx.event_set_target = 1;
    ctx.event_set_event = (int)MR_WAIT_OUT;
    ret = mr_waitable_raise(&able, MR_WAIT_IN, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(ctx.event_set_ret, 0);
    MR_TEST_ASSERT_EQ_INT(ctx.order_n, 1);
    MR_TEST_ASSERT_EQ_INT(ctx.id_order[0], 0);
    MR_TEST_ASSERT_EQ_INT(MR_WAITER_IS_WAITING(&waiters[1]), MR_FALSE);

    /* The waiter now only answers the new event */
    ret = mr_waiter_wait(&waiters[1], &able);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_waitable_raise(&able, MR_WAIT_IN, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(ctx.order_n, 2);
    MR_TEST_ASSERT_EQ_INT(ctx.id_order[1], 0);
    ret = mr_waitable_raise(&able, MR_WAIT_OUT, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(ctx.order_n, 3);
    MR_TEST_ASSERT_EQ_INT(ctx.id_order[2], 1);

    mr_waiter_del(&waiters[0]);
    mr_waiter_del(&waiters[1]);
    mr_waitable_del(&able);
}

/* Test: a waiter can be moved between waitables */
MR_TEST(wait, deep_two_waitables_sequential) {
    deep_wait_ctx_t ctx;
    deep_wait_arg_t arg;
    mr_waitable_t able1;
    mr_waitable_t able2;
    mr_waiter_t waiter;
    mr_err_t ret;

    deep_wait_reset(&ctx);
    ret = mr_waitable_init(&able1);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_waitable_init(&able2);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    arg.ctx = &ctx;
    arg.id = 0;
    ret = mr_waiter_init(&waiter, deep_wait_cb, &arg, MR_WAIT_IN);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ctx.waiters[0] = &waiter;
    ret = mr_waiter_wait(&waiter, &able1);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    ret = mr_waitable_raise(&able1, MR_WAIT_IN, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(ctx.order_n, 1);

    ret = mr_waiter_cancel(&waiter);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_waiter_wait(&waiter, &able2);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    ret = mr_waitable_raise(&able1, MR_WAIT_IN, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(ctx.order_n, 1);
    ret = mr_waitable_raise(&able2, MR_WAIT_IN, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(ctx.order_n, 2);

    mr_waiter_del(&waiter);
    mr_waitable_del(&able1);
    mr_waitable_del(&able2);
}

/* Test: a waiter can switch to another waitable from its own callback */
MR_TEST(wait, deep_wait_other_able_from_callback) {
    deep_wait_ctx_t ctx;
    deep_wait_arg_t arg;
    mr_waitable_t able1;
    mr_waitable_t able2;
    mr_waiter_t waiter;
    mr_err_t ret;

    deep_wait_reset(&ctx);
    ret = mr_waitable_init(&able1);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_waitable_init(&able2);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ctx.able = &able2;

    arg.ctx = &ctx;
    arg.id = 0;
    ret = mr_waiter_init(&waiter, deep_wait_cb, &arg, MR_WAIT_IN);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ctx.waiters[0] = &waiter;
    ctx.rewait_self = 0;
    ret = mr_waiter_wait(&waiter, &able1);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    /* The callback cancels the waiter and rewaits it on able2 */
    ret = mr_waitable_raise(&able1, MR_WAIT_IN, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(ctx.order_n, 1);
    MR_TEST_ASSERT_EQ_INT(ctx.wait_ret, 0);
    MR_TEST_ASSERT_EQ_INT(MR_WAITER_IS_WAITING(&waiter), MR_TRUE);

    ret = mr_waitable_raise(&able1, MR_WAIT_IN, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(ctx.order_n, 1);
    ret = mr_waitable_raise(&able2, MR_WAIT_IN, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(ctx.order_n, 2);

    ctx.rewait_self = -1;
    mr_waiter_del(&waiter);
    mr_waitable_del(&able1);
    mr_waitable_del(&able2);
}

/* Test: cancelling an unvisited waiter from a callback skips it without
 * disturbing the iteration of the others */
MR_TEST(wait, deep_cancel_mid_list_from_callback) {
    deep_wait_ctx_t ctx;
    deep_wait_arg_t args[5];
    mr_waitable_t able;
    mr_waiter_t waiters[5];
    mr_err_t ret;
    int i;

    deep_wait_reset(&ctx);
    ret = mr_waitable_init(&able);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    for (i = 0; i < 5; i++) {
        args[i].ctx = &ctx;
        args[i].id = i;
        ret = mr_waiter_init(&waiters[i], deep_wait_cb, &args[i], MR_WAIT_IN);
        MR_TEST_ASSERT_EQ_INT(ret, 0);
        ctx.waiters[i] = &waiters[i];
        ret = mr_waiter_wait(&waiters[i], &able);
        MR_TEST_ASSERT_EQ_INT(ret, 0);
    }

    ctx.cancel_target = 2;
    ret = mr_waitable_raise(&able, MR_WAIT_IN, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(ctx.order_n, 4);
    MR_TEST_ASSERT_EQ_INT(ctx.id_order[0], 0);
    MR_TEST_ASSERT_EQ_INT(ctx.id_order[1], 1);
    MR_TEST_ASSERT_EQ_INT(ctx.id_order[2], 3);
    MR_TEST_ASSERT_EQ_INT(ctx.id_order[3], 4);
    MR_TEST_ASSERT_EQ_INT(ctx.id_count[2], 0);
    MR_TEST_ASSERT_EQ_INT(MR_WAITER_IS_WAITING(&waiters[2]), MR_FALSE);

    for (i = 0; i < 5; i++) {
        mr_waiter_del(&waiters[i]);
    }
    mr_waitable_del(&able);
}

/* Test: a nested raise runs on its own cursor and leaves the outer raise
 * consistent */
MR_TEST(wait, deep_nested_raise_cursor_isolation) {
    deep_wait_ctx_t ctx;
    deep_wait_arg_t args[3];
    mr_waitable_t able;
    mr_waiter_t waiters[3];
    mr_err_t ret;
    int i;

    deep_wait_reset(&ctx);
    ret = mr_waitable_init(&able);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ctx.able = &able;

    for (i = 0; i < 3; i++) {
        args[i].ctx = &ctx;
        args[i].id = i;
        ret = mr_waiter_init(&waiters[i], deep_wait_nested_cb, &args[i],
                             MR_WAIT_IN);
        MR_TEST_ASSERT_EQ_INT(ret, 0);
        ctx.waiters[i] = &waiters[i];
        ret = mr_waiter_wait(&waiters[i], &able);
        MR_TEST_ASSERT_EQ_INT(ret, 0);
    }

    ret = mr_waitable_raise(&able, MR_WAIT_IN, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(ctx.raise_guard, 1);
    MR_TEST_ASSERT_EQ_INT(ctx.order_n, 6);
    MR_TEST_ASSERT_EQ_INT(ctx.id_order[0], 0);
    MR_TEST_ASSERT_EQ_INT(ctx.id_order[1], 0);
    MR_TEST_ASSERT_EQ_INT(ctx.id_order[2], 1);
    MR_TEST_ASSERT_EQ_INT(ctx.id_order[3], 2);
    MR_TEST_ASSERT_EQ_INT(ctx.id_order[4], 1);
    MR_TEST_ASSERT_EQ_INT(ctx.id_order[5], 2);

    for (i = 0; i < 3; i++) {
        mr_waiter_del(&waiters[i]);
    }
    mr_waitable_del(&able);
}

#endif /* defined(MR_USE_WAIT) */
#endif /* defined(MR_USE_TEST) */
