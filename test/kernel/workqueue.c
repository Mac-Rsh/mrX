/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/test/mr_test.h>
#if defined(MR_USE_TEST)
#if defined(MR_USE_WORK)
#include <mrX/kernel/mr_clock.h>
#include <mrX/kernel/mr_work.h>
#include <mrX/libc/mr_atomic.h>

typedef struct test_work_ctx {
    int run_count;
    mr_work_t *last_work;
    mr_work_t *chain_next;
} test_work_ctx_t;

static const mr_clazz_t test_work_clazz
    = MR_CLAZZ_INIT("test_work", MR_NULL, MR_CLAZZES_STATIK(work));

static void test_work_entry(struct mr_work *work, void *param) {
    test_work_ctx_t *ctx = param;

    if (ctx == MR_NULL) {
        return;
    }
    ctx->run_count++;
    ctx->last_work = work;
}

static void test_workqueue_chain_entry(struct mr_work *work, void *param) {
    test_work_ctx_t *ctx = param;

    ctx->run_count++;
    ctx->last_work = work;
    if (ctx->chain_next != MR_NULL) {
        mr_work_schedule(ctx->chain_next);
        ctx->chain_next = MR_NULL;
    }
}

/* Tests */
MR_TEST(workqueue, derive_init) {
    mr_workqueue_t queue;
    mr_err_t ret;

    ret = mr_workqueue_init(&queue);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_PTR(MR_OBJECT_CLAZZ(&queue),
                          MR_CLAZZES_STATIK(workqueue));
    ret = mr_workqueue_del(&queue);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
}

MR_TEST(workqueue, work_derive_init) {
    static mr_work_t work = MR_WORK_DERIVE_INIT(&work, &test_work_clazz,
                                                test_work_entry, MR_NULL);

    MR_TEST_ASSERT_EQ_PTR(MR_OBJECT_CLAZZ(&work), &test_work_clazz);
}

MR_TEST(workqueue, init_del) {
    mr_workqueue_t queue;
    mr_err_t ret;

    ret = mr_workqueue_init(&queue);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(MR_OBJECT_IS_INITED(&queue), MR_TRUE);

    ret = mr_workqueue_del(&queue);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(MR_OBJECT_IS_INITED(&queue), MR_FALSE);
}

MR_TEST(workqueue, create_del) {
    mr_workqueue_t *queue;
    mr_err_t ret;

    queue = mr_workqueue_create();
    MR_TEST_ASSERT_NE_PTR(queue, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(MR_OBJECT_IS_INITED(queue), MR_TRUE);

    ret = mr_workqueue_del(queue);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
}

MR_TEST(workqueue, work_init_del) {
    mr_work_t work;
    mr_err_t ret;

    ret = mr_work_init(&work, test_work_entry, (void *)0x1234);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(MR_OBJECT_IS_INITED(&work), MR_TRUE);

    ret = mr_work_del(&work);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(MR_OBJECT_IS_INITED(&work), MR_FALSE);
}

MR_TEST(workqueue, work_create_del) {
    mr_work_t *work;
    mr_err_t ret;

    work = mr_work_create(test_work_entry, (void *)0x5678);
    MR_TEST_ASSERT_NE_PTR(work, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(MR_OBJECT_IS_INITED(work), MR_TRUE);

    ret = mr_work_del(work);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
}

MR_TEST(workqueue, schedule_execute) {
    mr_workqueue_t queue;
    mr_work_t work;
    test_work_ctx_t ctx = {0};
    mr_err_t ret;

    ret = mr_workqueue_init(&queue);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    ret = mr_work_init(&work, test_work_entry, &ctx);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    ret = mr_work_schedule(&work);
    MR_TEST_ASSERT_EQ_INT(ret, -MR_ENOENT);

    ret = mr_work_bind(&work, &queue);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    ret = mr_work_schedule(&work);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    ctx.run_count = 0;
    ctx.last_work = MR_NULL;
    ret = mr_workqueue_execute(&queue);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(ctx.run_count, 1);
    MR_TEST_ASSERT_EQ_PTR(ctx.last_work, &work);

    ret = mr_work_del(&work);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    ret = mr_workqueue_del(&queue);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
}

MR_TEST(workqueue, delayed_schedule) {
    mr_work_t work;
    mr_work_t work1;
    mr_workqueue_t queue;
    mr_err_t ret;

    test_work_ctx_t ctx = {0};

    ret = mr_workqueue_init(&queue);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    ret = mr_work_init(&work, test_work_entry, &ctx);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    ret = mr_work_delayed_schedule(&work, 10);
    MR_TEST_ASSERT_EQ_INT(ret, -MR_ENOENT);

    ret = mr_work_bind(&work, &queue);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    ret = mr_work_init(&work1, test_work_entry, &ctx);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    ret = mr_work_bind(&work1, &queue);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    ret = mr_work_delayed_schedule(&work, 10);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    ret = mr_work_delayed_schedule(&work, 10);
    MR_TEST_ASSERT_EQ_INT(ret, -MR_EBUSY);

    ret = mr_work_delayed_schedule(&work1, 20);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    mr_clock_increase(10);

    ctx.run_count = 0;
    ctx.last_work = MR_NULL;
    ret = mr_workqueue_execute(&queue);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(ctx.run_count, 1);
    MR_TEST_ASSERT_EQ_PTR(ctx.last_work, &work);

    ret = mr_work_delayed_schedule(&work, 0);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    mr_clock_increase(10);

    mr_work_cancel(&work1);
    ret = mr_work_delayed_schedule(&work1, 10);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    ret = mr_workqueue_del(&queue);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    ret = mr_work_del(&work);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    ret = mr_work_del(&work1);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
}

MR_TEST(workqueue, work_cancel) {
    mr_workqueue_t queue;
    mr_work_t work;
    test_work_ctx_t ctx = {0};
    mr_err_t ret;

    ret = mr_workqueue_init(&queue);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    ret = mr_work_init(&work, test_work_entry, &ctx);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    ret = mr_work_bind(&work, &queue);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    ret = mr_work_schedule(&work);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    ret = mr_work_cancel(&work);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    ctx.run_count = 0;
    ret = mr_workqueue_execute(&queue);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(ctx.run_count, 0);

    ret = mr_work_del(&work);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    ret = mr_workqueue_del(&queue);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
}

MR_TEST(workqueue, work_is_running) {
    mr_workqueue_t queue;
    mr_work_t work;
    mr_err_t ret;

    ret = mr_workqueue_init(&queue);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    ret = mr_work_init(&work, test_work_entry, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    ret = mr_work_bind(&work, &queue);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    MR_TEST_ASSERT_EQ_INT(MR_WORK_IS_RUNNING(&work), MR_FALSE);

    ret = mr_work_schedule(&work);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    MR_TEST_ASSERT_EQ_INT(MR_WORK_IS_RUNNING(&work), MR_TRUE);

    ret = mr_work_schedule(&work);
    MR_TEST_ASSERT_NE_INT(ret, 0);

    ret = mr_work_cancel(&work);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    MR_TEST_ASSERT_EQ_INT(MR_WORK_IS_RUNNING(&work), MR_FALSE);

    ret = mr_work_del(&work);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    ret = mr_workqueue_del(&queue);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
}

MR_TEST(workqueue, del_pending_work) {
    mr_workqueue_t queue;
    mr_work_t work_now;
    mr_work_t work_delay;
    mr_err_t ret;

    ret = mr_workqueue_init(&queue);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    ret = mr_work_init(&work_now, test_work_entry, (void *)0x1111);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_work_init(&work_delay, test_work_entry, (void *)0x2222);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    ret = mr_work_bind(&work_now, &queue);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_work_bind(&work_delay, &queue);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    ret = mr_work_schedule(&work_now);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_work_delayed_schedule(&work_delay, 10);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    MR_TEST_ASSERT_EQ_INT(MR_WORK_IS_RUNNING(&work_now), MR_TRUE);
    MR_TEST_ASSERT_EQ_INT(MR_WORK_IS_RUNNING(&work_delay), MR_TRUE);

    ret = mr_workqueue_del(&queue);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(MR_OBJECT_IS_INITED(&queue), MR_FALSE);
    MR_TEST_ASSERT_EQ_INT(MR_WORK_IS_RUNNING(&work_now), MR_FALSE);
    MR_TEST_ASSERT_EQ_INT(MR_WORK_IS_RUNNING(&work_delay), MR_FALSE);

    ret = mr_work_del(&work_now);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_work_del(&work_delay);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
}

MR_TEST(workqueue, execute_batches) {
    mr_workqueue_t queue;
    mr_work_t work;
    mr_work_t next;
    test_work_ctx_t ctx = {0};
    mr_err_t ret;

    ret = mr_workqueue_init(&queue);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_work_init(&work, test_workqueue_chain_entry, &ctx);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_work_init(&next, test_work_entry, &ctx);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_work_bind(&work, &queue);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_work_bind(&next, &queue);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    ctx.chain_next = &next;
    ret = mr_workqueue_execute(&queue);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(ctx.run_count, 0);

    ret = mr_work_schedule(&work);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_workqueue_execute(&queue);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(ctx.run_count, 1);
    MR_TEST_ASSERT_EQ_INT(MR_WORK_IS_RUNNING(&next), MR_TRUE);

    ret = mr_workqueue_execute(&queue);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(ctx.run_count, 2);
    MR_TEST_ASSERT_EQ_INT(MR_WORK_IS_RUNNING(&next), MR_FALSE);

    ret = mr_work_del(&work);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_work_del(&next);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_workqueue_del(&queue);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
}

MR_TEST(workqueue, delayed_order_cancel) {
    mr_workqueue_t queue;
    mr_work_t late;
    mr_work_t early;
    mr_work_t canceled;
    test_work_ctx_t ctx = {0};
    mr_err_t ret;

    ret = mr_workqueue_init(&queue);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_work_init(&late, test_work_entry, &ctx);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_work_init(&early, test_work_entry, &ctx);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_work_init(&canceled, test_work_entry, &ctx);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_work_bind(&late, &queue);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_work_bind(&early, &queue);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_work_bind(&canceled, &queue);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    ret = mr_work_delayed_schedule(&late, 20);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_work_delayed_schedule(&early, 5);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_work_delayed_schedule(&canceled, 3);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_work_cancel(&canceled);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    mr_clock_increase(4);
    MR_TEST_ASSERT_EQ_INT(ctx.run_count, 0);
    ret = mr_workqueue_execute(&queue);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(ctx.run_count, 0);

    mr_clock_increase(1);
    ret = mr_workqueue_execute(&queue);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(ctx.run_count, 1);
    MR_TEST_ASSERT_EQ_PTR(ctx.last_work, &early);

    mr_clock_increase(15);
    ret = mr_workqueue_execute(&queue);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(ctx.run_count, 2);
    MR_TEST_ASSERT_EQ_PTR(ctx.last_work, &late);

    ret = mr_work_del(&late);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_work_del(&early);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_work_del(&canceled);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_workqueue_del(&queue);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
}

MR_TEST(workqueue, dying_rejects_work) {
    mr_workqueue_t queue;
    mr_work_t work;
    mr_err_t ret;

    ret = mr_workqueue_init(&queue);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_work_init(&work, test_work_entry, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_work_bind(&work, &queue);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    mr_atomic_store(&queue.dying, MR_TRUE);
    ret = mr_workqueue_work(&queue, &work);
    MR_TEST_ASSERT_EQ_INT(ret, -MR_ENOENT);
    ret = mr_workqueue_delayed_work(&queue, &work, 1);
    MR_TEST_ASSERT_EQ_INT(ret, -MR_ENOENT);

    mr_atomic_store(&queue.dying, MR_FALSE);
    ret = mr_work_del(&work);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_workqueue_del(&queue);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
}

MR_TEST(workqueue, get_put) {
    mr_workqueue_t queue;
    mr_work_t work;
    mr_err_t ret;

    ret = mr_workqueue_init(&queue);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_NE_PTR(mr_workqueue_get(&queue), MR_NULL);
    MR_TEST_ASSERT_EQ_INT(mr_workqueue_put(&queue), MR_FALSE);

    ret = mr_work_init(&work, test_work_entry, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_work_bind(&work, &queue);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_NE_PTR(mr_work_get(&work), MR_NULL);
    MR_TEST_ASSERT_EQ_INT(mr_work_put(&work), MR_FALSE);

    ret = mr_work_del(&work);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_workqueue_del(&queue);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
}

MR_TEST(workqueue, cancel_idle) {
    mr_workqueue_t queue;
    mr_work_t work;
    test_work_ctx_t ctx = {0};
    mr_err_t ret;

    ret = mr_workqueue_init(&queue);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_work_init(&work, test_work_entry, &ctx);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_work_bind(&work, &queue);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    ret = mr_work_cancel(&work);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(MR_WORK_IS_RUNNING(&work), MR_FALSE);
    ret = mr_workqueue_execute(&queue);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(ctx.run_count, 0);

    ret = mr_work_del(&work);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_workqueue_del(&queue);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
}

#if defined(MR_USE_WORKQUEUE_HOOK)
typedef struct test_hook_ctx {
    int suspend_called;
    int wakeup_called;
} test_hook_ctx_t;

static void test_hook_suspend(struct mr_workqueue *queue, void *param) {
    test_hook_ctx_t *ctx = param;

    MR_IGNORE(queue);
    ctx->suspend_called = 1;
}

static void test_hook_wakeup(struct mr_workqueue *queue, void *param) {
    test_hook_ctx_t *ctx = param;

    MR_IGNORE(queue);
    ctx->wakeup_called = 1;
}

MR_TEST(workqueue, hook) {
    test_hook_ctx_t hook_ctx = {0};
    mr_workqueue_hook_t hook = {.suspend = test_hook_suspend,
                                .wakeup = test_hook_wakeup,
                                .param = &hook_ctx};
    mr_work_t work;
    mr_work_t work1;
    mr_workqueue_t queue;
    test_work_ctx_t ctx = {0};
    mr_err_t ret;

    ret = mr_workqueue_init(&queue);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    mr_workqueue_hook_set(&queue, &hook);

    ret = mr_work_init(&work, test_work_entry, &ctx);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    ret = mr_work_bind(&work, &queue);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    ret = mr_work_init(&work1, test_work_entry, &ctx);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    ret = mr_work_bind(&work1, &queue);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    hook_ctx.wakeup_called = 0;
    ret = mr_work_schedule(&work);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(hook_ctx.wakeup_called, 1);

    ret = mr_work_schedule(&work1);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(hook_ctx.wakeup_called, 1);

    hook_ctx.suspend_called = 0;
    ret = mr_workqueue_execute(&queue);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(hook_ctx.suspend_called, 1);

    ret = mr_work_del(&work);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    ret = mr_work_del(&work1);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    ret = mr_workqueue_del(&queue);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
}

#endif /* defined(MR_USE_WORKQUEUE_HOOK) */
#endif /* defined(MR_USE_WORK) */
#endif /* defined(MR_USE_TEST) */
