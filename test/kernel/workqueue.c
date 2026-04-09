/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/test/mr_test.h>
#if defined(MR_USE_TEST)
#if defined(MR_USE_WORKQUEUE)
#include <mrX/kernel/mr_workqueue.h>

static void *last_param = MR_NULL;
static int work_run_count = 0;
static const mr_clazz_t test_workqueue_clazz =
    MR_CLAZZ_INIT("test_workqueue", MR_NULL,
                  MR_CLAZZES_STATIK(MR_CLAZZES_FIND(workqueue)));
static const mr_clazz_t test_work_clazz =
    MR_CLAZZ_INIT("test_work", MR_NULL,
                  MR_CLAZZES_STATIK(MR_CLAZZES_FIND(work)));

static void test_work_entry(struct mr_work *work, void *param) {
    MR_IGNORE(work);
    work_run_count++;
    last_param = param;
}

/* -------------------------------------------------------------------------- */
/* Test: Workqueue derive init */
static void test_workqueue_derive_init(void) {
    static mr_workqueue_t queue = MR_WORKQUEUE_DERIVE_INIT(&queue,
                                                           &test_workqueue_clazz);

    MR_TEST_ASSERT_EQ_PTR(MR_OBJECT_CLAZZ(&queue), &test_workqueue_clazz);
}
MR_TEST_EXPORT(workqueue, derive_init, test_workqueue_derive_init);

/* -------------------------------------------------------------------------- */
/* Test: Work derive init */
static void test_work_derive_init(void) {
    static mr_work_t work = MR_WORK_DERIVE_INIT(&work, &test_work_clazz,
                                                test_work_entry, MR_NULL);

    MR_TEST_ASSERT_EQ_PTR(MR_OBJECT_CLAZZ(&work), &test_work_clazz);
}
MR_TEST_EXPORT(workqueue, work_derive_init, test_work_derive_init);

/* -------------------------------------------------------------------------- */
/* Test: Workqueue init / del */
static void test_workqueue_init_del(void) {
    mr_workqueue_t queue;
    mr_err_t ret;

    ret = mr_workqueue_init(&queue);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(MR_OBJECT_IS_INITED(&queue), MR_TRUE);

    ret = mr_workqueue_del(&queue);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(MR_OBJECT_IS_INITED(&queue), MR_FALSE);
}
MR_TEST_EXPORT(workqueue, init_del, test_workqueue_init_del);

/* -------------------------------------------------------------------------- */
/* Test: Workqueue create / del */
static void test_workqueue_create_del(void) {
    mr_workqueue_t *queue;
    mr_err_t ret;

    queue = mr_workqueue_create();
    MR_TEST_ASSERT_NE_PTR(queue, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(MR_OBJECT_IS_INITED(queue), MR_TRUE);

    ret = mr_workqueue_del(queue);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
}
MR_TEST_EXPORT(workqueue, create_del, test_workqueue_create_del);

/* -------------------------------------------------------------------------- */
/* Test: Work init / del */
static void test_work_init_del(void) {
    mr_work_t work;
    mr_err_t ret;

    ret = mr_work_init(&work, test_work_entry, (void *)0x1234);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(MR_OBJECT_IS_INITED(&work), MR_TRUE);

    ret = mr_work_del(&work);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(MR_OBJECT_IS_INITED(&work), MR_FALSE);
}
MR_TEST_EXPORT(workqueue, work_init_del, test_work_init_del);

/* -------------------------------------------------------------------------- */
/* Test: Work create / del */
static void test_work_create_del(void) {
    mr_work_t *work;
    mr_err_t ret;

    work = mr_work_create(test_work_entry, (void *)0x5678);
    MR_TEST_ASSERT_NE_PTR(work, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(MR_OBJECT_IS_INITED(work), MR_TRUE);

    ret = mr_work_del(work);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
}
MR_TEST_EXPORT(workqueue, work_create_del, test_work_create_del);

/* -------------------------------------------------------------------------- */
/* Test: Work bind / schedule / execute */
static void test_work_schedule_execute(void) {
    mr_workqueue_t queue;
    mr_work_t work;
    mr_err_t ret;

    ret = mr_workqueue_init(&queue);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    ret = mr_work_init(&work, test_work_entry, (void *)0xABCD);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    ret = mr_work_schedule(&work);
    MR_TEST_ASSERT_EQ_INT(ret, -MR_ENOENT);

    ret = mr_work_bind(&work, &queue);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    ret = mr_work_schedule(&work);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    work_run_count = 0;
    ret = mr_workqueue_execute(&queue);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(work_run_count, 1);
    MR_TEST_ASSERT_EQ_PTR(last_param, (void *)0xABCD);

    ret = mr_work_del(&work);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    ret = mr_workqueue_del(&queue);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
}
MR_TEST_EXPORT(workqueue, schedule_execute, test_work_schedule_execute);

/* -------------------------------------------------------------------------- */
/* Test: Work delayed schedule */
static void test_work_delayed_schedule(void) {
    mr_work_t work, work1;
    mr_workqueue_t queue;
    mr_err_t ret;

    ret = mr_workqueue_init(&queue);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    ret = mr_work_init(&work, test_work_entry, (void *)0xDEAD);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    ret = mr_work_delayed_schedule(&work, 10);
    MR_TEST_ASSERT_EQ_INT(ret, -MR_ENOENT);

    ret = mr_work_bind(&work, &queue);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    ret = mr_work_init(&work1, test_work_entry, (void *)0xDEAD);
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

    work_run_count = 0;
    ret = mr_workqueue_execute(&queue);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(work_run_count, 1);
    MR_TEST_ASSERT_EQ_PTR(last_param, (void *)0xDEAD);

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
MR_TEST_EXPORT(workqueue, delayed_schedule, test_work_delayed_schedule);

/* -------------------------------------------------------------------------- */
/* Test: Work cancel */
static void test_work_cancel(void) {
    mr_workqueue_t queue;
    mr_work_t work;
    mr_err_t ret;

    ret = mr_workqueue_init(&queue);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    ret = mr_work_init(&work, test_work_entry, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    ret = mr_work_bind(&work, &queue);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    ret = mr_work_schedule(&work);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    ret = mr_work_cancel(&work);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    work_run_count = 0;
    ret = mr_workqueue_execute(&queue);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(work_run_count, 0);

    ret = mr_work_del(&work);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    ret = mr_workqueue_del(&queue);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
}
MR_TEST_EXPORT(workqueue, work_cancel, test_work_cancel);

/* -------------------------------------------------------------------------- */
/* Test: Work is running */
static void test_work_is_running(void) {
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
MR_TEST_EXPORT(workqueue, work_is_running, test_work_is_running);

/* -------------------------------------------------------------------------- */
/* Test: Workqueue delete with pending work                                   */
static void test_workqueue_del_pending_work(void) {
    mr_workqueue_t queue;
    mr_work_t work_now, work_delay;
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
MR_TEST_EXPORT(workqueue, del_pending_work, test_workqueue_del_pending_work);

#if defined(MR_USE_WORKQUEUE_HOOK)
static int hook_suspend_called = 0;
static int hook_wakeup_called = 0;

static void test_hook_suspend(struct mr_workqueue *queue, void *param) {
    MR_IGNORE(queue);
    MR_IGNORE(param);
    hook_suspend_called = 1;
}

static void test_hook_wakeup(struct mr_workqueue *queue, void *param) {
    MR_IGNORE(queue);
    MR_IGNORE(param);
    hook_wakeup_called = 1;
}

static void test_workqueue_hook(void) {
    mr_workqueue_hook_t hook = {.suspend = test_hook_suspend,
        .wakeup = test_hook_wakeup,
        .param = MR_NULL};
    mr_work_t work, work1;
    mr_workqueue_t queue;
    mr_err_t ret;

    ret = mr_workqueue_init(&queue);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    mr_workqueue_hook_set(&queue, &hook);

    ret = mr_work_init(&work, test_work_entry, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    ret = mr_work_bind(&work, &queue);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    ret = mr_work_init(&work1, test_work_entry, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    ret = mr_work_bind(&work1, &queue);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    hook_wakeup_called = 0;
    ret = mr_work_schedule(&work);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(hook_wakeup_called, 1);

    ret = mr_work_schedule(&work1);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(hook_wakeup_called, 1);

    hook_suspend_called = 0;
    ret = mr_workqueue_execute(&queue);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(hook_suspend_called, 1);

    ret = mr_work_del(&work);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    ret = mr_work_del(&work1);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    ret = mr_workqueue_del(&queue);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
}
MR_TEST_EXPORT(workqueue, hook, test_workqueue_hook);
#endif /* defined(MR_USE_WORKQUEUE_HOOK) */
#endif /* defined(MR_USE_WORKQUEUE) */
#endif /* defined(MR_USE_TEST) */
