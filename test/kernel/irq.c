/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/test/mr_test.h>
#if defined(MR_USE_TEST)
#if defined(MR_USE_IRQ)
#include <mrX/kernel/mr_irq.h>

static mr_uint8_t last_priority = 0;
static mr_uint8_t last_type = 0;
static mr_int32_t last_irq = -1;
static int handle_count = 0;
static int enable_count = 0;
static int disable_count = 0;
static int unmask_count = 0;
static int mask_count = 0;

/* Helpers */
static void test_irq_reset(void) {
    last_priority = 0;
    last_type = 0;
    last_irq = -1;
    handle_count = 0;
    enable_count = 0;
    disable_count = 0;
    unmask_count = 0;
    mask_count = 0;
}

static void irq_priority_set(mr_uint32_t irq, mr_uint8_t priority, void *priv) {
    MR_IGNORE(priv);

    last_irq = (mr_int32_t)irq;
    last_priority = priority;
}

static void irq_type_set(mr_uint32_t irq, mr_uint8_t type, void *priv) {
    MR_IGNORE(priv);

    last_irq = (mr_int32_t)irq;
    last_type = type;
}

static void irq_enable(mr_uint32_t irq, void *priv) {
    MR_IGNORE(priv);

    last_irq = (mr_int32_t)irq;
    enable_count++;
}

static void irq_unmask(mr_uint32_t irq, void *priv) {
    MR_IGNORE(irq);
    MR_IGNORE(priv);

    unmask_count++;
}

static void irq_disable(mr_uint32_t irq, void *priv) {
    MR_IGNORE(priv);

    last_irq = (mr_int32_t)irq;
    disable_count++;
}

static void irq_mask(mr_uint32_t irq, void *priv) {
    MR_IGNORE(irq);
    MR_IGNORE(priv);

    mask_count++;
}

static mr_irq_ops_t mock_irq_ops = {
    .priority_set = irq_priority_set,
    .type_set = irq_type_set,
    .enable = irq_enable,
    .unmask = irq_unmask,
    .disable = irq_disable,
    .mask = irq_mask,
};

static mr_irq_return_t test_irq_handler(mr_uint32_t irq, void *owner) {
    MR_IGNORE(irq);
    MR_IGNORE(owner);
    handle_count++;
    return MR_IRQ_HANDLED;
}

/* Tests */
MR_TEST(irq, init_del) {
    mr_irq_t irq;
    mr_err_t ret;

    ret = mr_irq_init(&irq, 0, 1, &mock_irq_ops, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(MR_OBJECT_IS_INITED(&irq), MR_TRUE);
    MR_TEST_ASSERT_EQ_INT(irq.istart, 0);
    MR_TEST_ASSERT_EQ_INT(irq.iend, 1);

    ret = mr_irq_del(&irq);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(MR_OBJECT_IS_INITED(&irq), MR_FALSE);
}

MR_TEST(irq, create_del) {
    mr_irq_t *irq;
    mr_err_t ret;

    irq = mr_irq_create(0, 1, &mock_irq_ops, MR_NULL);
    MR_TEST_ASSERT_NE_PTR(irq, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(MR_OBJECT_IS_INITED(irq), MR_TRUE);
    MR_TEST_ASSERT_EQ_INT(irq->istart, 0);
    MR_TEST_ASSERT_EQ_INT(irq->iend, 1);

    ret = mr_irq_del(irq);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
}

MR_TEST(irq, request_free) {
    mr_irq_t irq;
    mr_err_t ret;

    test_irq_reset();

    ret = mr_irq_request(0, test_irq_handler, &irq, MR_IRQ_EDGE_RISING);
    MR_TEST_ASSERT_NE_INT(ret, 0);

    ret = mr_irq_init(&irq, 0, 0, &mock_irq_ops, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    test_irq_reset();
    ret = mr_irq_request(0, test_irq_handler, &irq, MR_IRQ_EDGE_RISING);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(last_irq, 0);
    MR_TEST_ASSERT_EQ_INT(last_type, MR_IRQ_EDGE_RISING);
    MR_TEST_ASSERT_EQ_INT(last_priority, 0);

    test_irq_reset();
    ret = mr_irq_free(0, &irq);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(last_irq, 0);

    ret = mr_irq_request(0, test_irq_handler, &irq, MR_IRQ_EDGE_RISING);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_irq_del(&irq);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    ret = mr_irq_init(&irq, 0, 0, &mock_irq_ops, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_irq_del(&irq);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
}

MR_TEST(irq, priority_set_get) {
    mr_irq_t irq;
    mr_err_t ret;
    int priority;

    ret = mr_irq_init(&irq, 0, 0, &mock_irq_ops, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    test_irq_reset();
    ret = mr_irq_priority_set(0, 5);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(last_irq, 0);
    MR_TEST_ASSERT_EQ_INT(last_priority, 5);

    priority = mr_irq_priority(0);
    MR_TEST_ASSERT_EQ_INT(priority, 5);

    ret = mr_irq_del(&irq);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
}

MR_TEST(irq, type_get) {
    mr_irq_t irq;
    mr_err_t ret;
    int type;

    ret = mr_irq_init(&irq, 0, 0, &mock_irq_ops, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    ret = mr_irq_request(0, test_irq_handler, &irq, MR_IRQ_EDGE_FALLING);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    type = mr_irq_type(0);
    MR_TEST_ASSERT_EQ_INT(type, MR_IRQ_EDGE_FALLING);

    ret = mr_irq_free(0, &irq);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    ret = mr_irq_del(&irq);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
}

MR_TEST(irq, enable_disable) {
    mr_irq_t irq;
    mr_err_t ret;

    ret = mr_irq_init(&irq, 0, 0, &mock_irq_ops, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    ret = mr_irq_request(0, test_irq_handler, &irq, MR_IRQ_LEVEL_HIGH);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    test_irq_reset();
    ret = mr_irq_disable(0);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(last_irq, 0);

    test_irq_reset();
    ret = mr_irq_enable(0);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(last_irq, 0);

    ret = mr_irq_free(0, &irq);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    ret = mr_irq_del(&irq);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
}

MR_TEST(irq, handle) {
    mr_irq_t irq;
    mr_err_t ret;
    mr_irq_return_t result;

    ret = mr_irq_init(&irq, 0, 0, &mock_irq_ops, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    ret = mr_irq_request(0, test_irq_handler, &irq, MR_IRQ_EDGE_BOTH);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    test_irq_reset();
    result = mr_irq_handle(0);
    MR_TEST_ASSERT_EQ_INT(result, MR_IRQ_HANDLED);
    MR_TEST_ASSERT_EQ_INT(handle_count, 1);

    ret = mr_irq_free(0, &irq);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    ret = mr_irq_del(&irq);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
}

static mr_irq_return_t test_shared_handler1(mr_uint32_t irq, void *owner) {
    mr_irq_free(irq, owner);
    return MR_IRQ_NONE;
}

static mr_irq_return_t test_shared_handler2(mr_uint32_t irq, void *owner) {
    mr_irq_free(irq, owner);
    return MR_IRQ_HANDLED;
}

MR_TEST(irq, shared) {
    mr_irq_t irq;
    mr_err_t ret;

    test_irq_reset();

    ret = mr_irq_init(&irq, 0, 0, &mock_irq_ops, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    ret = mr_irq_request(0, test_shared_handler1, test_shared_handler1,
                         MR_IRQ_EDGE_RISING);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_irq_request(0, test_shared_handler2, test_shared_handler2,
                         MR_IRQ_SHARED | MR_IRQ_EDGE_RISING);
    MR_TEST_ASSERT_NE_INT(ret, 0);
    ret = mr_irq_free(0, test_shared_handler1);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    ret = mr_irq_request(0, test_shared_handler1, test_shared_handler2,
                         MR_IRQ_SHARED | MR_IRQ_EDGE_RISING);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_irq_request(0, test_shared_handler2, test_shared_handler1,
                         MR_IRQ_SHARED | MR_IRQ_EDGE_RISING);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    mr_irq_handle(0);

    ret = mr_irq_request(0, test_shared_handler1, test_shared_handler1,
                         MR_IRQ_SHARED | MR_IRQ_EDGE_RISING);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_irq_request(0, test_shared_handler2, test_shared_handler2,
                         MR_IRQ_SHARED | MR_IRQ_EDGE_RISING);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_irq_request(0, test_shared_handler2, test_shared_handler2,
                         MR_IRQ_SHARED | MR_IRQ_EDGE_RISING);
    MR_TEST_ASSERT_NE_INT(ret, 0);
    ret = mr_irq_free(0, test_shared_handler2);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    ret = mr_irq_request(0, test_shared_handler1, test_shared_handler1,
                         MR_IRQ_EDGE_RISING);
    MR_TEST_ASSERT_NE_INT(ret, 0);
    ret = mr_irq_request(0, test_shared_handler2, test_shared_handler2,
                         MR_IRQ_SHARED | MR_IRQ_EDGE_FALLING);
    MR_TEST_ASSERT_NE_INT(ret, 0);

    ret = mr_irq_free(0, test_shared_handler1);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    ret = mr_irq_del(&irq);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
}

MR_TEST(irq, error_cases) {
    mr_irq_t irq;
    mr_irq_t irq1;
    mr_err_t ret;

    test_irq_reset();

    ret = mr_irq_init(&irq, 0, 0, &mock_irq_ops, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_irq_init(&irq1, 0, 0, &mock_irq_ops, MR_NULL);
    MR_TEST_ASSERT_NE_INT(ret, 0);
    ret = mr_irq_del(&irq);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    ret = mr_irq_init(&irq, 1, 2, &mock_irq_ops, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_irq_init(&irq1, 0, 2, &mock_irq_ops, MR_NULL);
    MR_TEST_ASSERT_NE_INT(ret, 0);
    ret = mr_irq_del(&irq);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    ret = mr_irq_init(&irq, 0, 0, &mock_irq_ops, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_irq_request(0, test_irq_handler, &irq, MR_IRQ_EDGE_RISING);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_irq_free(0, &irq);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_irq_free(0, &irq);
    MR_TEST_ASSERT_NE_INT(ret, 0);
    ret = mr_irq_del(&irq);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
}

/* Test: unregistered irq operations return their documented empty result */
MR_TEST(irq, unregistered_operations) {
    static int owner;

    MR_TEST_ASSERT_EQ_INT(mr_irq_free(7, &owner), -MR_ENOENT);
    MR_TEST_ASSERT_EQ_INT(mr_irq_enable(7), -MR_ENOENT);
    MR_TEST_ASSERT_EQ_INT(mr_irq_disable(7), -MR_ENOENT);
    MR_TEST_ASSERT_EQ_INT(mr_irq_priority_set(7, 3), -MR_ENOENT);
    MR_TEST_ASSERT_EQ_INT(mr_irq_type(7), -MR_ENOENT);
    MR_TEST_ASSERT_EQ_INT(mr_irq_priority(7), -MR_ENOENT);
    MR_TEST_ASSERT_EQ_INT(mr_irq_handle(7), MR_IRQ_NONE);
}

/* Test: repeated enable and disable requests keep the depth within bounds */
MR_TEST(irq, depth_saturation_without_action) {
    mr_irq_t irq;
    mr_err_t ret;

    ret = mr_irq_init(&irq, 9, 9, &mock_irq_ops, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    test_irq_reset();
    MR_TEST_ASSERT_EQ_INT(mr_irq_enable(9), 0);
    MR_TEST_ASSERT_EQ_INT(enable_count, 1);
    MR_TEST_ASSERT_EQ_INT(unmask_count, 1);
    MR_TEST_ASSERT_EQ_INT(mr_irq_enable(9), 0);
    MR_TEST_ASSERT_EQ_INT(enable_count, 1);
    MR_TEST_ASSERT_EQ_INT(unmask_count, 1);

    MR_TEST_ASSERT_EQ_INT(mr_irq_disable(9), 0);
    MR_TEST_ASSERT_EQ_INT(disable_count, 1);
    MR_TEST_ASSERT_EQ_INT(mask_count, 1);
    MR_TEST_ASSERT_EQ_INT(mr_irq_disable(9), 0);
    MR_TEST_ASSERT_EQ_INT(disable_count, 1);
    MR_TEST_ASSERT_EQ_INT(mask_count, 1);

    ret = mr_irq_del(&irq);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
}

MR_TEST(irq, get_put) {
    mr_irq_t irq;
    mr_err_t ret;

    test_irq_reset();
    ret = mr_irq_init(&irq, 8, 8, &mock_irq_ops, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_NE_PTR(mr_irq_get(&irq), MR_NULL);
    MR_TEST_ASSERT_EQ_INT(mr_irq_put(&irq), MR_FALSE);
    ret = mr_irq_del(&irq);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
}

#if defined(MR_USE_IRQ_DEFER)
#if defined(MR_USE_IRQ_DEFER_HOOK)
static int irq_hook_status = 0;

static void test_irq_defer_hook_reset(void) {
    irq_hook_status = 0;
}

static void irq_hook_suspend(void *param) {
    MR_IGNORE(param);
    irq_hook_status = 0;
}

static void irq_hook_wakeup(void *param) {
    MR_IGNORE(param);
    irq_hook_status = 1;
}

static mr_irq_defer_hook_t mock_irq_hook
    = {.suspend = irq_hook_suspend, .wakeup = irq_hook_wakeup};
#endif /* defined(MR_USE_IRQ_DEFER_HOOK) */

static int defer_handle_count = 0;

static void test_irq_defer_reset(void) {
    test_irq_reset();
    defer_handle_count = 0;
#if defined(MR_USE_IRQ_DEFER_HOOK)
    test_irq_defer_hook_reset();
#endif /* defined(MR_USE_IRQ_DEFER_HOOK) */
}

static mr_irq_return_t test_defer_handler(mr_uint32_t irq, void *owner) {
    MR_IGNORE(irq);
    MR_IGNORE(owner);
    return MR_IRQ_DEFERRED;
}

static mr_irq_return_t test_defer_complete_handler(mr_uint32_t irq,
                                                   void *owner) {
    MR_IGNORE(irq);
    MR_IGNORE(owner);
    return MR_IRQ_DEFERRED;
}

static mr_irq_return_t test_defer_complete_entry(mr_uint32_t irq, void *owner) {
    MR_IGNORE(irq);
    MR_IGNORE(owner);
    defer_handle_count++;
    return MR_IRQ_HANDLED;
}

MR_TEST(irq, defer_request) {
    mr_irq_t irq;
    mr_err_t ret;

#if defined(MR_USE_IRQ_DEFER_HOOK)
    mr_irq_defer_hook_set(&mock_irq_hook);
#endif /* defined(MR_USE_IRQ_DEFER_HOOK) */
    test_irq_defer_reset();

    ret = mr_irq_init(&irq, 0, 0, &mock_irq_ops, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    ret = mr_irq_defer_request(0, test_defer_handler, &irq, MR_IRQ_EDGE_RISING,
                               test_irq_handler);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

#if defined(MR_USE_IRQ_DEFER_HOOK)
    test_irq_defer_hook_reset();
#endif /* defined(MR_USE_IRQ_DEFER_HOOK) */
    mr_irq_handle(0);
#if defined(MR_USE_IRQ_DEFER_HOOK)
    MR_TEST_ASSERT_EQ_INT(irq_hook_status, 1);
#endif /* defined(MR_USE_IRQ_DEFER_HOOK) */
    mr_irq_defer_execute();
#if defined(MR_USE_IRQ_DEFER_HOOK)
    MR_TEST_ASSERT_EQ_INT(irq_hook_status, 0);
#endif /* defined(MR_USE_IRQ_DEFER_HOOK) */
    MR_TEST_ASSERT_EQ_INT(handle_count, 1);

    ret = mr_irq_free(0, &irq);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    ret = mr_irq_del(&irq);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
}

MR_TEST(irq, defer_free_before_execute) {
    mr_irq_t irq;
    mr_err_t ret;

#if defined(MR_USE_IRQ_DEFER_HOOK)
    mr_irq_defer_hook_set(&mock_irq_hook);
#endif /* defined(MR_USE_IRQ_DEFER_HOOK) */

    test_irq_defer_reset();
    ret = mr_irq_init(&irq, 0, 0, &mock_irq_ops, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    ret = mr_irq_defer_request(0, test_defer_complete_handler, &irq,
                               MR_IRQ_EDGE_RISING, test_defer_complete_entry);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    MR_TEST_ASSERT_EQ_INT(mr_irq_handle(0), MR_IRQ_DEFERRED);
    ret = mr_irq_free(0, &irq);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    mr_irq_defer_execute();
    MR_TEST_ASSERT_EQ_INT(defer_handle_count, 0);
#if defined(MR_USE_IRQ_DEFER_HOOK)
    MR_TEST_ASSERT_EQ_INT(irq_hook_status, 0);
#endif /* defined(MR_USE_IRQ_DEFER_HOOK) */

    ret = mr_irq_del(&irq);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
}

MR_TEST(irq, defer_del_before_execute) {
    mr_irq_t irq;
    mr_err_t ret;

#if defined(MR_USE_IRQ_DEFER_HOOK)
    mr_irq_defer_hook_set(&mock_irq_hook);
#endif /* defined(MR_USE_IRQ_DEFER_HOOK) */

    test_irq_defer_reset();
    ret = mr_irq_init(&irq, 0, 0, &mock_irq_ops, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    ret = mr_irq_defer_request(0, test_defer_complete_handler, &irq,
                               MR_IRQ_EDGE_RISING, test_defer_complete_entry);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    MR_TEST_ASSERT_EQ_INT(mr_irq_handle(0), MR_IRQ_DEFERRED);
    ret = mr_irq_del(&irq);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    mr_irq_defer_execute();
    MR_TEST_ASSERT_EQ_INT(defer_handle_count, 0);
#if defined(MR_USE_IRQ_DEFER_HOOK)
    MR_TEST_ASSERT_EQ_INT(irq_hook_status, 0);
#endif /* defined(MR_USE_IRQ_DEFER_HOOK) */
}

#endif /* defined(MR_USE_IRQ_DEFER) */
#endif /* defined(MR_USE_IRQ) */
#endif /* defined(MR_USE_TEST) */
