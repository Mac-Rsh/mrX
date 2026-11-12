/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2026-08-30        MacRsh        Deep test version
 */

#include <mrX/test/mr_test.h>
#if defined(MR_USE_TEST)
#if defined(MR_USE_WORK)
#include <mrX/kernel/mr_clock.h>
#include <mrX/kernel/mr_work.h>
#include <mrX/libc/mr_atomic.h>

/* Helpers */
typedef struct deep_work_ctx {
    int total;
    mr_tick_t base_tick;
    int id_order[8];
    int order_n;
    int tick_fired[4];
    mr_workqueue_t *queue;
    mr_work_t *works[4];
    int cancel_target;
    int del_target;
    int resubmit_limit;
    int resubmit_ret;
    int self_del_ret;
} deep_work_ctx_t;

typedef struct deep_work_arg {
    deep_work_ctx_t *ctx;
    int id;
} deep_work_arg_t;

static void deep_work_record(deep_work_ctx_t *ctx, int id) {
    if (ctx->order_n < 8) {
        ctx->id_order[ctx->order_n] = id;
    }
    ctx->order_n++;
    ctx->total++;
    if ((id >= 0) && (id < 4)) {
        ctx->tick_fired[id] = (int)(mr_clock_tick() - ctx->base_tick);
    }
}

static void deep_work_entry(struct mr_work *work, void *param) {
    deep_work_arg_t *arg = param;

    MR_IGNORE(work);
    deep_work_record(arg->ctx, arg->id);
}

static void deep_work_cancel_sibling_entry(struct mr_work *work, void *param) {
    deep_work_arg_t *arg = param;
    deep_work_ctx_t *ctx = arg->ctx;

    MR_IGNORE(work);
    deep_work_record(ctx, arg->id);
    if (ctx->cancel_target >= 0) {
        mr_work_cancel(ctx->works[ctx->cancel_target]);
        ctx->cancel_target = -1;
    }
}

static void deep_work_del_sibling_entry(struct mr_work *work, void *param) {
    deep_work_arg_t *arg = param;
    deep_work_ctx_t *ctx = arg->ctx;

    MR_IGNORE(work);
    deep_work_record(ctx, arg->id);
    if (ctx->del_target >= 0) {
        mr_work_del(ctx->works[ctx->del_target]);
        ctx->del_target = -1;
    }
}

static void deep_work_resubmit_entry(struct mr_work *work, void *param) {
    deep_work_arg_t *arg = param;
    deep_work_ctx_t *ctx = arg->ctx;

    deep_work_record(ctx, arg->id);
    if (ctx->total < ctx->resubmit_limit) {
        ctx->resubmit_ret = (int)mr_work_schedule(work);
    }
}

static void deep_work_self_del_entry(struct mr_work *work, void *param) {
    deep_work_arg_t *arg = param;
    deep_work_ctx_t *ctx = arg->ctx;

    deep_work_record(ctx, arg->id);
    ctx->self_del_ret = (int)mr_work_del(work);
}

static void deep_work_reset(deep_work_ctx_t *ctx) {
    int i;

    ctx->total = 0;
    ctx->base_tick = mr_clock_tick();
    ctx->order_n = 0;
    ctx->queue = MR_NULL;
    ctx->cancel_target = -1;
    ctx->del_target = -1;
    ctx->resubmit_limit = 0;
    ctx->resubmit_ret = 0;
    ctx->self_del_ret = 0;
    for (i = 0; i < 4; i++) {
        ctx->works[i] = MR_NULL;
        ctx->tick_fired[i] = -1;
        ctx->id_order[i] = -1;
    }
}

MR_TEST(work, deep_batch_fifo_order) {
    deep_work_ctx_t ctx;
    deep_work_arg_t args[4];
    mr_workqueue_t queue;
    mr_work_t works[4];
    mr_err_t ret;
    int i;

    deep_work_reset(&ctx);
    ret = mr_workqueue_init(&queue);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ctx.queue = &queue;

    for (i = 0; i < 4; i++) {
        args[i].ctx = &ctx;
        args[i].id = i;
        ret = mr_work_init(&works[i], deep_work_entry, &args[i]);
        MR_TEST_ASSERT_EQ_INT(ret, 0);
        ctx.works[i] = &works[i];
        ret = mr_work_bind(&works[i], &queue);
        MR_TEST_ASSERT_EQ_INT(ret, 0);
        ret = mr_work_schedule(&works[i]);
        MR_TEST_ASSERT_EQ_INT(ret, 0);
    }

    ret = mr_workqueue_execute(&queue);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(ctx.total, 4);
    MR_TEST_ASSERT_EQ_INT(ctx.order_n, 4);
    MR_TEST_ASSERT_EQ_INT(ctx.id_order[0], 0);
    MR_TEST_ASSERT_EQ_INT(ctx.id_order[1], 1);
    MR_TEST_ASSERT_EQ_INT(ctx.id_order[2], 2);
    MR_TEST_ASSERT_EQ_INT(ctx.id_order[3], 3);
    for (i = 0; i < 4; i++) {
        MR_TEST_ASSERT_EQ_INT(MR_WORK_IS_RUNNING(&works[i]), MR_FALSE);
    }

    /* A drained queue executes to a no-op */
    ret = mr_workqueue_execute(&queue);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(ctx.total, 4);

    for (i = 0; i < 4; i++) {
        MR_TEST_ASSERT_EQ_INT(mr_work_del(&works[i]), 0);
    }
    MR_TEST_ASSERT_EQ_INT(mr_workqueue_del(&queue), 0);
}

MR_TEST(work, deep_cancel_sibling_from_work) {
    deep_work_ctx_t ctx;
    deep_work_arg_t args[3];
    mr_workqueue_t queue;
    mr_work_t works[3];
    mr_err_t ret;

    deep_work_reset(&ctx);
    ret = mr_workqueue_init(&queue);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ctx.queue = &queue;
    ctx.cancel_target = 1;

    args[0].ctx = &ctx;
    args[0].id = 0;
    args[1].ctx = &ctx;
    args[1].id = 1;
    args[2].ctx = &ctx;
    args[2].id = 2;
    ret = mr_work_init(&works[0], deep_work_cancel_sibling_entry, &args[0]);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_work_init(&works[1], deep_work_entry, &args[1]);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_work_init(&works[2], deep_work_entry, &args[2]);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    ctx.works[0] = &works[0];
    ctx.works[1] = &works[1];
    ctx.works[2] = &works[2];
    MR_TEST_ASSERT_EQ_INT(mr_work_bind(&works[0], &queue), 0);
    MR_TEST_ASSERT_EQ_INT(mr_work_bind(&works[1], &queue), 0);
    MR_TEST_ASSERT_EQ_INT(mr_work_bind(&works[2], &queue), 0);
    MR_TEST_ASSERT_EQ_INT(mr_work_schedule(&works[0]), 0);
    MR_TEST_ASSERT_EQ_INT(mr_work_schedule(&works[1]), 0);
    MR_TEST_ASSERT_EQ_INT(mr_work_schedule(&works[2]), 0);

    ret = mr_workqueue_execute(&queue);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(ctx.total, 2);
    MR_TEST_ASSERT_EQ_INT(ctx.id_order[0], 0);
    MR_TEST_ASSERT_EQ_INT(ctx.id_order[1], 2);
    MR_TEST_ASSERT_EQ_INT(MR_WORK_IS_RUNNING(&works[1]), MR_FALSE);

    MR_TEST_ASSERT_EQ_INT(mr_work_del(&works[0]), 0);
    MR_TEST_ASSERT_EQ_INT(mr_work_del(&works[1]), 0);
    MR_TEST_ASSERT_EQ_INT(mr_work_del(&works[2]), 0);
    MR_TEST_ASSERT_EQ_INT(mr_workqueue_del(&queue), 0);
}

MR_TEST(work, deep_del_sibling_from_work) {
    deep_work_ctx_t ctx;
    deep_work_arg_t args[2];
    mr_workqueue_t queue;
    mr_work_t works[2];
    mr_err_t ret;

    deep_work_reset(&ctx);
    ret = mr_workqueue_init(&queue);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ctx.queue = &queue;
    ctx.del_target = 1;

    args[0].ctx = &ctx;
    args[0].id = 0;
    args[1].ctx = &ctx;
    args[1].id = 1;
    ret = mr_work_init(&works[0], deep_work_del_sibling_entry, &args[0]);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_work_init(&works[1], deep_work_entry, &args[1]);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ctx.works[0] = &works[0];
    ctx.works[1] = &works[1];
    MR_TEST_ASSERT_EQ_INT(mr_work_bind(&works[0], &queue), 0);
    MR_TEST_ASSERT_EQ_INT(mr_work_bind(&works[1], &queue), 0);
    MR_TEST_ASSERT_EQ_INT(mr_work_schedule(&works[0]), 0);
    MR_TEST_ASSERT_EQ_INT(mr_work_schedule(&works[1]), 0);

    ret = mr_workqueue_execute(&queue);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(ctx.total, 1);
    MR_TEST_ASSERT_EQ_INT(ctx.id_order[0], 0);
    MR_TEST_ASSERT_EQ_INT(MR_OBJECT_IS_INITED(&works[1]), MR_FALSE);

    MR_TEST_ASSERT_EQ_INT(mr_work_del(&works[0]), 0);
    MR_TEST_ASSERT_EQ_INT(mr_workqueue_del(&queue), 0);
}

MR_TEST(work, deep_self_resubmit_from_work) {
    deep_work_ctx_t ctx;
    deep_work_arg_t arg;
    mr_workqueue_t queue;
    mr_work_t work;
    mr_err_t ret;

    deep_work_reset(&ctx);
    ret = mr_workqueue_init(&queue);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ctx.queue = &queue;
    ctx.resubmit_limit = 3;

    arg.ctx = &ctx;
    arg.id = 0;
    ret = mr_work_init(&work, deep_work_resubmit_entry, &arg);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ctx.works[0] = &work;
    MR_TEST_ASSERT_EQ_INT(mr_work_bind(&work, &queue), 0);
    MR_TEST_ASSERT_EQ_INT(mr_work_schedule(&work), 0);

    ret = mr_workqueue_execute(&queue);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(ctx.total, 1);
    MR_TEST_ASSERT_EQ_INT(MR_WORK_IS_RUNNING(&work), MR_TRUE);

    ret = mr_workqueue_execute(&queue);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(ctx.total, 2);
    MR_TEST_ASSERT_EQ_INT(MR_WORK_IS_RUNNING(&work), MR_TRUE);

    ret = mr_workqueue_execute(&queue);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(ctx.total, 3);
    MR_TEST_ASSERT_EQ_INT(ctx.resubmit_ret, 0);
    MR_TEST_ASSERT_EQ_INT(MR_WORK_IS_RUNNING(&work), MR_FALSE);

    /* No more resubmission past the limit */
    ret = mr_workqueue_execute(&queue);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(ctx.total, 3);

    MR_TEST_ASSERT_EQ_INT(mr_work_del(&work), 0);
    MR_TEST_ASSERT_EQ_INT(mr_workqueue_del(&queue), 0);
}

MR_TEST(work, deep_self_del_from_work) {
    deep_work_ctx_t ctx;
    deep_work_arg_t arg;
    mr_workqueue_t queue;
    mr_work_t work;
    mr_err_t ret;

    deep_work_reset(&ctx);
    ret = mr_workqueue_init(&queue);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ctx.queue = &queue;

    arg.ctx = &ctx;
    arg.id = 0;
    ret = mr_work_init(&work, deep_work_self_del_entry, &arg);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ctx.works[0] = &work;
    MR_TEST_ASSERT_EQ_INT(mr_work_bind(&work, &queue), 0);
    MR_TEST_ASSERT_EQ_INT(mr_work_schedule(&work), 0);

    ret = mr_workqueue_execute(&queue);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(ctx.total, 1);
    MR_TEST_ASSERT_EQ_INT(ctx.self_del_ret, 0);
    MR_TEST_ASSERT_EQ_INT(MR_OBJECT_IS_INITED(&work), MR_FALSE);

    MR_TEST_ASSERT_EQ_INT(mr_workqueue_del(&queue), 0);
}

MR_TEST(work, deep_rebind_second_queue) {
    deep_work_ctx_t ctx;
    deep_work_arg_t arg;
    mr_workqueue_t queue1;
    mr_workqueue_t queue2;
    mr_work_t work;
    mr_err_t ret;

    deep_work_reset(&ctx);
    ret = mr_workqueue_init(&queue1);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_workqueue_init(&queue2);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ctx.queue = &queue1;

    arg.ctx = &ctx;
    arg.id = 0;
    ret = mr_work_init(&work, deep_work_entry, &arg);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ctx.works[0] = &work;
    MR_TEST_ASSERT_EQ_INT(mr_work_bind(&work, &queue1), 0);
    MR_TEST_ASSERT_EQ_INT(mr_work_schedule(&work), 0);
    MR_TEST_ASSERT_EQ_INT(mr_workqueue_execute(&queue1), 0);
    MR_TEST_ASSERT_EQ_INT(ctx.total, 1);

    /* Direct submission to queue2 updates the work binding */
    ret = mr_workqueue_work(&queue2, &work);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(mr_work_cancel(&work), 0);
    ret = mr_work_schedule(&work);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    ret = mr_workqueue_execute(&queue1);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(ctx.total, 1);
    ret = mr_workqueue_execute(&queue2);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(ctx.total, 2);
    MR_TEST_ASSERT_EQ_INT(MR_WORK_IS_RUNNING(&work), MR_FALSE);

    MR_TEST_ASSERT_EQ_INT(mr_work_del(&work), 0);
    MR_TEST_ASSERT_EQ_INT(mr_workqueue_del(&queue1), 0);
    MR_TEST_ASSERT_EQ_INT(mr_workqueue_del(&queue2), 0);
}

MR_TEST(work, deep_delayed_fifo_same_tick) {
    deep_work_ctx_t ctx;
    deep_work_arg_t args[3];
    mr_workqueue_t queue;
    mr_work_t works[3];
    mr_err_t ret;
    int i;

    deep_work_reset(&ctx);
    ret = mr_workqueue_init(&queue);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ctx.queue = &queue;

    for (i = 0; i < 3; i++) {
        args[i].ctx = &ctx;
        args[i].id = i;
        ret = mr_work_init(&works[i], deep_work_entry, &args[i]);
        MR_TEST_ASSERT_EQ_INT(ret, 0);
        ctx.works[i] = &works[i];
        MR_TEST_ASSERT_EQ_INT(mr_work_bind(&works[i], &queue), 0);
        ret = mr_work_delayed_schedule(&works[i], 5);
        MR_TEST_ASSERT_EQ_INT(ret, 0);
    }

    for (i = 0; i < 5; i++) {
        mr_clock_increase(1);
        MR_TEST_ASSERT_EQ_INT(mr_workqueue_execute(&queue), 0);
        MR_TEST_EXPECT_EQ_INT(ctx.total, i == 4 ? 3 : 0);
    }
    MR_TEST_ASSERT_EQ_INT(ctx.order_n, 3);
    MR_TEST_ASSERT_EQ_INT(ctx.id_order[0], 0);
    MR_TEST_ASSERT_EQ_INT(ctx.id_order[1], 1);
    MR_TEST_ASSERT_EQ_INT(ctx.id_order[2], 2);

    for (i = 0; i < 3; i++) {
        MR_TEST_ASSERT_EQ_INT(mr_work_del(&works[i]), 0);
    }
    MR_TEST_ASSERT_EQ_INT(mr_workqueue_del(&queue), 0);
}

MR_TEST(work, deep_delayed_out_of_order_deadlines) {
    deep_work_ctx_t ctx;
    deep_work_arg_t args[3];
    mr_workqueue_t queue;
    mr_work_t works[3];
    mr_err_t ret;
    int i;

    deep_work_reset(&ctx);
    ret = mr_workqueue_init(&queue);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ctx.queue = &queue;

    for (i = 0; i < 3; i++) {
        args[i].ctx = &ctx;
        args[i].id = i;
        ret = mr_work_init(&works[i], deep_work_entry, &args[i]);
        MR_TEST_ASSERT_EQ_INT(ret, 0);
        ctx.works[i] = &works[i];
        MR_TEST_ASSERT_EQ_INT(mr_work_bind(&works[i], &queue), 0);
    }

    /* Insert late, then middle, then early */
    MR_TEST_ASSERT_EQ_INT(mr_work_delayed_schedule(&works[2], 20), 0);
    MR_TEST_ASSERT_EQ_INT(mr_work_delayed_schedule(&works[1], 12), 0);
    MR_TEST_ASSERT_EQ_INT(mr_work_delayed_schedule(&works[0], 5), 0);

    for (i = 0; i < 20; i++) {
        mr_clock_increase(1);
        MR_TEST_ASSERT_EQ_INT(mr_workqueue_execute(&queue), 0);
    }
    MR_TEST_ASSERT_EQ_INT(ctx.order_n, 3);
    MR_TEST_ASSERT_EQ_INT(ctx.id_order[0], 0);
    MR_TEST_ASSERT_EQ_INT(ctx.id_order[1], 1);
    MR_TEST_ASSERT_EQ_INT(ctx.id_order[2], 2);
    /* The realtime tick thread can only make the clock run early, so the
       relative fire tick is at least the requested delay */
    MR_TEST_EXPECT_GE_INT(ctx.tick_fired[0], 5);
    MR_TEST_EXPECT_GE_INT(ctx.tick_fired[1], 12);
    MR_TEST_EXPECT_GE_INT(ctx.tick_fired[2], 20);
    MR_TEST_EXPECT_LE_INT(ctx.tick_fired[0], 5 + 128);
    MR_TEST_EXPECT_LE_INT(ctx.tick_fired[1], 12 + 128);
    MR_TEST_EXPECT_LE_INT(ctx.tick_fired[2], 20 + 128);

    for (i = 0; i < 3; i++) {
        MR_TEST_ASSERT_EQ_INT(mr_work_del(&works[i]), 0);
    }
    MR_TEST_ASSERT_EQ_INT(mr_workqueue_del(&queue), 0);
}

MR_TEST(work, deep_delayed_cancel_rearm) {
    deep_work_ctx_t ctx;
    deep_work_arg_t args[2];
    mr_workqueue_t queue;
    mr_work_t works[2];
    mr_err_t ret;
    int i;

    deep_work_reset(&ctx);
    ret = mr_workqueue_init(&queue);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ctx.queue = &queue;

    args[0].ctx = &ctx;
    args[0].id = 0;
    args[1].ctx = &ctx;
    args[1].id = 1;
    ret = mr_work_init(&works[0], deep_work_entry, &args[0]);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_work_init(&works[1], deep_work_entry, &args[1]);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ctx.works[0] = &works[0];
    ctx.works[1] = &works[1];
    MR_TEST_ASSERT_EQ_INT(mr_work_bind(&works[0], &queue), 0);
    MR_TEST_ASSERT_EQ_INT(mr_work_bind(&works[1], &queue), 0);

    MR_TEST_ASSERT_EQ_INT(mr_work_delayed_schedule(&works[0], 10), 0);
    MR_TEST_ASSERT_EQ_INT(mr_work_cancel(&works[0]), 0);
    MR_TEST_ASSERT_EQ_INT(MR_WORK_IS_RUNNING(&works[0]), MR_FALSE);

    /* The timer rearms from the stopped state for the new delayed work */
    MR_TEST_ASSERT_EQ_INT(mr_work_delayed_schedule(&works[1], 3), 0);
    for (i = 0; i < 12; i++) {
        mr_clock_increase(1);
        MR_TEST_ASSERT_EQ_INT(mr_workqueue_execute(&queue), 0);
    }
    MR_TEST_ASSERT_EQ_INT(ctx.order_n, 1);
    MR_TEST_ASSERT_EQ_INT(ctx.id_order[0], 1);
    MR_TEST_EXPECT_GE_INT(ctx.tick_fired[1], 3);
    MR_TEST_EXPECT_LE_INT(ctx.tick_fired[1], 3 + 128);
    MR_TEST_ASSERT_EQ_INT(ctx.tick_fired[0], -1);

    MR_TEST_ASSERT_EQ_INT(mr_work_del(&works[0]), 0);
    MR_TEST_ASSERT_EQ_INT(mr_work_del(&works[1]), 0);
    MR_TEST_ASSERT_EQ_INT(mr_workqueue_del(&queue), 0);
}

MR_TEST(work, deep_delayed_zero_mixes_with_batch) {
    deep_work_ctx_t ctx;
    deep_work_arg_t args[2];
    mr_workqueue_t queue;
    mr_work_t works[2];
    mr_err_t ret;

    deep_work_reset(&ctx);
    ret = mr_workqueue_init(&queue);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ctx.queue = &queue;

    args[0].ctx = &ctx;
    args[0].id = 0;
    args[1].ctx = &ctx;
    args[1].id = 1;
    ret = mr_work_init(&works[0], deep_work_entry, &args[0]);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_work_init(&works[1], deep_work_entry, &args[1]);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ctx.works[0] = &works[0];
    ctx.works[1] = &works[1];
    MR_TEST_ASSERT_EQ_INT(mr_work_bind(&works[0], &queue), 0);
    MR_TEST_ASSERT_EQ_INT(mr_work_bind(&works[1], &queue), 0);

    MR_TEST_ASSERT_EQ_INT(mr_work_delayed_schedule(&works[0], 0), 0);
    MR_TEST_ASSERT_EQ_INT(MR_WORK_IS_RUNNING(&works[0]), MR_TRUE);
    MR_TEST_ASSERT_EQ_INT(mr_work_schedule(&works[1]), 0);

    /* A delayed request on a pending work is rejected as busy */
    MR_TEST_EXPECT_EQ_INT(mr_work_delayed_schedule(&works[1], 5), -MR_EBUSY);

    ret = mr_workqueue_execute(&queue);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(ctx.total, 2);
    MR_TEST_ASSERT_EQ_INT(ctx.id_order[0], 0);
    MR_TEST_ASSERT_EQ_INT(ctx.id_order[1], 1);

    MR_TEST_ASSERT_EQ_INT(mr_work_del(&works[0]), 0);
    MR_TEST_ASSERT_EQ_INT(mr_work_del(&works[1]), 0);
    MR_TEST_ASSERT_EQ_INT(mr_workqueue_del(&queue), 0);
}

MR_TEST(work, deep_queue_reinit_and_rebind) {
    deep_work_ctx_t ctx;
    deep_work_arg_t arg;
    mr_workqueue_t queue;
    mr_workqueue_t queue2;
    mr_work_t work;
    mr_err_t ret;

    deep_work_reset(&ctx);
    ret = mr_workqueue_init(&queue);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ctx.queue = &queue;

    arg.ctx = &ctx;
    arg.id = 0;
    ret = mr_work_init(&work, deep_work_entry, &arg);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ctx.works[0] = &work;
    MR_TEST_ASSERT_EQ_INT(mr_work_bind(&work, &queue), 0);

    MR_TEST_ASSERT_EQ_INT(mr_workqueue_del(&queue), 0);
    ret = mr_work_schedule(&work);
    MR_TEST_ASSERT_EQ_INT(ret, -MR_ENOENT);

    /* Reinitialize the same static queue and rebind the work */
    ret = mr_workqueue_init(&queue);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(MR_OBJECT_IS_INITED(&queue), MR_TRUE);
    ret = mr_work_bind(&work, &queue);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(mr_work_schedule(&work), 0);
    MR_TEST_ASSERT_EQ_INT(mr_workqueue_execute(&queue), 0);
    MR_TEST_ASSERT_EQ_INT(ctx.total, 1);

    /* A second queue keeps working after the first was recycled */
    ret = mr_workqueue_init(&queue2);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_workqueue_work(&queue2, &work);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(mr_workqueue_execute(&queue2), 0);
    MR_TEST_ASSERT_EQ_INT(ctx.total, 2);

    MR_TEST_ASSERT_EQ_INT(mr_work_del(&work), 0);
    MR_TEST_ASSERT_EQ_INT(mr_workqueue_del(&queue), 0);
    MR_TEST_ASSERT_EQ_INT(mr_workqueue_del(&queue2), 0);
}

#if defined(MR_USE_TEST_PTHREAD)
#include <pthread.h>

#define DEEP_WORK_MT_THREADS (4)
#define DEEP_WORK_MT_ITERS   (250)

typedef struct deep_work_mt_arg {
    deep_work_arg_t arg;
    mr_workqueue_t *queue;
    int offset;
} deep_work_mt_arg_t;

static void *deep_work_mt_producer(void *argument) {
    deep_work_mt_arg_t *mt_arg = argument;
    static mr_work_t works[DEEP_WORK_MT_THREADS * DEEP_WORK_MT_ITERS];
    int base = mt_arg->offset * DEEP_WORK_MT_ITERS;
    int i;

    for (i = 0; i < DEEP_WORK_MT_ITERS; i++) {
        if (mr_work_init(&works[base + i], deep_work_entry, &mt_arg->arg)
            != 0) {
            return MR_NULL;
        }
        if (mr_work_bind(&works[base + i], mt_arg->queue) != 0) {
            return MR_NULL;
        }
        if (mr_work_schedule(&works[base + i]) != 0) {
            return MR_NULL;
        }
    }
    return MR_NULL;
}

MR_TEST(work, deep_concurrent_submit) {
    deep_work_mt_arg_t mt_args[DEEP_WORK_MT_THREADS];
    pthread_t threads[DEEP_WORK_MT_THREADS];
    deep_work_ctx_t ctx;
    mr_workqueue_t queue;
    int created = 0;
    int spins = 0;
    int i;
    int ret;

    deep_work_reset(&ctx);
    ret = mr_workqueue_init(&queue);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ctx.queue = &queue;

    for (i = 0; i < DEEP_WORK_MT_THREADS; i++) {
        mt_args[i].arg.ctx = &ctx;
        mt_args[i].arg.id = i;
        mt_args[i].queue = &queue;
        mt_args[i].offset = i;
        ret = pthread_create(&threads[i], MR_NULL, deep_work_mt_producer,
                             &mt_args[i]);
        MR_TEST_EXPECT_EQ_INT(ret, 0);
        if (ret != 0) {
            break;
        }
        created++;
    }
    for (i = 0; i < created; i++) {
        MR_TEST_EXPECT_EQ_INT(pthread_join(threads[i], MR_NULL), 0);
    }
    if (created != DEEP_WORK_MT_THREADS) {
        MR_TEST_ASSERT_EQ_INT(mr_workqueue_del(&queue), 0);
        return;
    }

    /* All submissions are finished, one drain must run every work */
    while ((ctx.total < DEEP_WORK_MT_THREADS * DEEP_WORK_MT_ITERS)
           && (spins < 4)) {
        MR_TEST_EXPECT_EQ_INT(mr_workqueue_execute(&queue), 0);
        spins++;
    }
    MR_TEST_ASSERT_EQ_INT(ctx.total, DEEP_WORK_MT_THREADS * DEEP_WORK_MT_ITERS);
    MR_TEST_ASSERT_EQ_INT(mr_workqueue_del(&queue), 0);
}
#endif /* defined(MR_USE_TEST_PTHREAD) */

#endif /* defined(MR_USE_WORK) */
#endif /* defined(MR_USE_TEST) */
