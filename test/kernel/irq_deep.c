/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2026-08-30        MacRsh        Deep test version
 */

#include <mrX/test/mr_test.h>
#if defined(MR_USE_TEST)
#if defined(MR_USE_IRQ)
#include <mrX/kernel/mr_irq.h>

/* Helpers */
#define DEEP_IRQ_MAX_OWNERS            (4)

/* Owners are encoded as (id + 1) so a handler can decode its index */
static void *deep_irq_owner(int id) {
    return (void *)((mr_intptr_t)(id + 1));
}

static int deep_irq_id(void *owner) {
    return (int)(mr_intptr_t)owner - 1;
}

static int deep_irq_enable_count = 0;
static int deep_irq_disable_count = 0;
static int deep_irq_mask_count = 0;
static int deep_irq_unmask_count = 0;
static mr_uint32_t deep_irq_last_number = 0;
static mr_uint8_t deep_irq_last_priority = 0;
static int deep_irq_last_type = -1;

static void deep_irq_reset_ops(void) {
    deep_irq_enable_count = 0;
    deep_irq_disable_count = 0;
    deep_irq_mask_count = 0;
    deep_irq_unmask_count = 0;
    deep_irq_last_number = 0;
    deep_irq_last_priority = 0;
    deep_irq_last_type = -1;
}

static void deep_irq_op_priority_set(mr_uint32_t irq, mr_uint8_t priority,
                                     void *priv) {
    MR_IGNORE(priv);
    deep_irq_last_number = irq;
    deep_irq_last_priority = priority;
}

static void deep_irq_op_type_set(mr_uint32_t irq, mr_uint8_t type, void *priv) {
    MR_IGNORE(priv);
    deep_irq_last_number = irq;
    deep_irq_last_type = (int)type;
}

static void deep_irq_op_enable(mr_uint32_t irq, void *priv) {
    MR_IGNORE(irq);
    MR_IGNORE(priv);
    deep_irq_enable_count++;
}

static void deep_irq_op_disable(mr_uint32_t irq, void *priv) {
    MR_IGNORE(irq);
    MR_IGNORE(priv);
    deep_irq_disable_count++;
}

static void deep_irq_op_mask(mr_uint32_t irq, void *priv) {
    MR_IGNORE(irq);
    MR_IGNORE(priv);
    deep_irq_mask_count++;
}

static void deep_irq_op_unmask(mr_uint32_t irq, void *priv) {
    MR_IGNORE(irq);
    MR_IGNORE(priv);
    deep_irq_unmask_count++;
}

static const mr_irq_ops_t deep_irq_ops = {
    .priority_set = deep_irq_op_priority_set,
    .type_set = deep_irq_op_type_set,
    .enable = deep_irq_op_enable,
    .unmask = deep_irq_op_unmask,
    .disable = deep_irq_op_disable,
    .mask = deep_irq_op_mask,
};

/* Shared handler bookkeeping */
static int deep_irq_handler_count[DEEP_IRQ_MAX_OWNERS];
static int deep_irq_handler_order[8];
static int deep_irq_handler_order_n = 0;

static void deep_irq_reset_handlers(void) {
    int i;

    for (i = 0; i < DEEP_IRQ_MAX_OWNERS; i++) {
        deep_irq_handler_count[i] = 0;
    }
    for (i = 0; i < 8; i++) {
        deep_irq_handler_order[i] = -1;
    }
    deep_irq_handler_order_n = 0;
}

static void deep_irq_handler_record(void *owner) {
    int id = deep_irq_id(owner);

    if ((id >= 0) && (id < DEEP_IRQ_MAX_OWNERS)) {
        deep_irq_handler_count[id]++;
    }
    if (deep_irq_handler_order_n < 8) {
        deep_irq_handler_order[deep_irq_handler_order_n++] = id;
    }
}

static mr_irq_return_t deep_irq_handler_none(mr_uint32_t irq, void *owner) {
    MR_IGNORE(irq);
    deep_irq_handler_record(owner);
    return MR_IRQ_NONE;
}

static mr_irq_return_t deep_irq_handler_handled(mr_uint32_t irq, void *owner) {
    MR_IGNORE(irq);
    deep_irq_handler_record(owner);
    return MR_IRQ_HANDLED;
}

static int deep_irq_defer_count = 0;
static int deep_irq_defer_main_count = 0;
static int deep_irq_nested_depth = 0;
static int deep_irq_nested_ret = 0;
static int deep_irq_del_in_handler = 0;
static int deep_irq_del_in_handler_ret = 0;
static mr_irq_t *deep_irq_del_target = MR_NULL;

static void deep_irq_reset_special(void) {
    deep_irq_defer_count = 0;
    deep_irq_defer_main_count = 0;
    deep_irq_nested_depth = 0;
    deep_irq_nested_ret = 0;
    deep_irq_del_in_handler = 0;
    deep_irq_del_in_handler_ret = 0;
    deep_irq_del_target = MR_NULL;
}

static mr_irq_return_t deep_irq_handler_deferred(mr_uint32_t irq, void *owner) {
    MR_IGNORE(irq);
    MR_IGNORE(owner);
    deep_irq_defer_main_count++;
    return MR_IRQ_DEFERRED;
}

static mr_irq_return_t deep_irq_defer_entry(mr_uint32_t irq, void *owner) {
    MR_IGNORE(irq);
    MR_IGNORE(owner);
    deep_irq_defer_count++;
    return MR_IRQ_HANDLED;
}

/* Handler that triggers one nested handle of the same irq */
static mr_irq_return_t deep_irq_handler_nested(mr_uint32_t irq, void *owner) {
    MR_IGNORE(owner);
    deep_irq_nested_depth++;
    if (deep_irq_nested_depth == 1) {
        deep_irq_nested_ret = (int)mr_irq_handle(irq);
    }
    return MR_IRQ_HANDLED;
}

/* Handler that deletes its own irq controller */
static mr_irq_return_t deep_irq_handler_del(mr_uint32_t irq, void *owner) {
    MR_IGNORE(irq);
    MR_IGNORE(owner);
    deep_irq_del_in_handler++;
    deep_irq_del_in_handler_ret = (int)mr_irq_del(deep_irq_del_target);
    return MR_IRQ_HANDLED;
}

/* Tests */

/* Test: the first and last irq numbers of the table are fully usable */
MR_TEST(irq, deep_boundary_numbers) {
    mr_irq_t irq0;
    mr_irq_t irq15;
    mr_err_t ret;

    deep_irq_reset_ops();
    deep_irq_reset_handlers();

    ret = mr_irq_init(&irq0, 0, 0, &deep_irq_ops, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_irq_init(&irq15, 15, 15, &deep_irq_ops, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    MR_TEST_ASSERT_EQ_INT(mr_irq_request(0, deep_irq_handler_handled,
                                         deep_irq_owner(0), MR_IRQ_LEVEL_HIGH),
                          0);
    MR_TEST_ASSERT_EQ_INT(mr_irq_request(15, deep_irq_handler_handled,
                                         deep_irq_owner(1), MR_IRQ_LEVEL_HIGH),
                          0);

    MR_TEST_ASSERT_EQ_INT(mr_irq_handle(0), MR_IRQ_HANDLED);
    MR_TEST_ASSERT_EQ_INT(mr_irq_handle(15), MR_IRQ_HANDLED);
    MR_TEST_ASSERT_EQ_INT(deep_irq_handler_count[0], 1);
    MR_TEST_ASSERT_EQ_INT(deep_irq_handler_count[1], 1);

    MR_TEST_ASSERT_EQ_INT(mr_irq_free(0, deep_irq_owner(0)), 0);
    MR_TEST_ASSERT_EQ_INT(mr_irq_free(15, deep_irq_owner(1)), 0);
    MR_TEST_ASSERT_EQ_INT(mr_irq_del(&irq0), 0);
    MR_TEST_ASSERT_EQ_INT(mr_irq_del(&irq15), 0);
}

/* Test: a failed overlapping range insert leaves untouched numbers free */
MR_TEST(irq, deep_range_insert_rollback) {
    mr_irq_t irq_a;
    mr_irq_t irq_b;
    mr_irq_t irq_c;
    mr_err_t ret;

    deep_irq_reset_ops();

    ret = mr_irq_init(&irq_a, 5, 6, &deep_irq_ops, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    /* Overlap inside the new range must fail */
    ret = mr_irq_init(&irq_b, 4, 8, &deep_irq_ops, MR_NULL);
    MR_TEST_ASSERT_NE_INT(ret, 0);

    /* Numbers below the conflict point must have been rolled back */
    ret = mr_irq_init(&irq_b, 4, 4, &deep_irq_ops, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    /* Numbers owned by the first irq must still be occupied */
    ret = mr_irq_init(&irq_c, 5, 5, &deep_irq_ops, MR_NULL);
    MR_TEST_ASSERT_NE_INT(ret, 0);
    ret = mr_irq_init(&irq_c, 6, 6, &deep_irq_ops, MR_NULL);
    MR_TEST_ASSERT_NE_INT(ret, 0);

    MR_TEST_ASSERT_EQ_INT(mr_irq_del(&irq_a), 0);
    MR_TEST_ASSERT_EQ_INT(mr_irq_del(&irq_b), 0);
}

/* Test: duplicate owner is rejected and a busy line rejects new owners */
MR_TEST(irq, deep_double_request_and_busy) {
    mr_irq_t irq;
    mr_err_t ret;

    deep_irq_reset_ops();

    ret = mr_irq_init(&irq, 10, 10, &deep_irq_ops, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    ret = mr_irq_request(10, deep_irq_handler_none, deep_irq_owner(0),
                         MR_IRQ_EDGE_RISING);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_irq_request(10, deep_irq_handler_none, deep_irq_owner(0),
                         MR_IRQ_EDGE_RISING);
    /* The share flag check precedes the owner check so a non-shared line
       reports busy even for a duplicate owner */
    MR_TEST_ASSERT_EQ_INT(ret, -MR_EBUSY);
    ret = mr_irq_request(10, deep_irq_handler_none, deep_irq_owner(1),
                         MR_IRQ_EDGE_RISING);
    MR_TEST_ASSERT_EQ_INT(ret, -MR_EBUSY);

    MR_TEST_ASSERT_EQ_INT(mr_irq_free(10, deep_irq_owner(0)), 0);
    ret = mr_irq_request(10, deep_irq_handler_none, deep_irq_owner(1),
                         MR_IRQ_EDGE_RISING);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(mr_irq_free(10, deep_irq_owner(1)), 0);
    MR_TEST_ASSERT_EQ_INT(mr_irq_del(&irq), 0);
}

/* Test: the first request resets priority to default and later shared
 * requests keep a custom priority */
MR_TEST(irq, deep_priority_default_and_persist) {
    mr_irq_t irq;
    mr_err_t ret;

    deep_irq_reset_ops();

    ret = mr_irq_init(&irq, 11, 11, &deep_irq_ops, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    MR_TEST_ASSERT_EQ_INT(mr_irq_priority_set(11, 5), 0);
    MR_TEST_ASSERT_EQ_INT(mr_irq_priority(11), 5);

    /* The first request installs the default priority again */
    MR_TEST_ASSERT_EQ_INT(mr_irq_request(11, deep_irq_handler_none,
                                         deep_irq_owner(0),
                                         MR_IRQ_SHARED | MR_IRQ_LEVEL_HIGH), 0);
    MR_TEST_ASSERT_EQ_INT(mr_irq_priority(11), 0);
    MR_TEST_ASSERT_EQ_INT(deep_irq_last_priority, 0);

    /* A custom priority survives a further shared request */
    MR_TEST_ASSERT_EQ_INT(mr_irq_priority_set(11, 7), 0);
    MR_TEST_ASSERT_EQ_INT(mr_irq_request(11, deep_irq_handler_none,
                                         deep_irq_owner(1),
                                         MR_IRQ_SHARED | MR_IRQ_LEVEL_HIGH), 0);
    MR_TEST_ASSERT_EQ_INT(mr_irq_priority(11), 7);

    MR_TEST_ASSERT_EQ_INT(mr_irq_free(11, deep_irq_owner(0)), 0);
    MR_TEST_ASSERT_EQ_INT(mr_irq_free(11, deep_irq_owner(1)), 0);
    MR_TEST_ASSERT_EQ_INT(mr_irq_del(&irq), 0);
}

/* Test: freeing the last action rearms the line type on the next request */
MR_TEST(irq, deep_type_rearm_after_free) {
    mr_irq_t irq;
    mr_err_t ret;

    deep_irq_reset_ops();

    ret = mr_irq_init(&irq, 12, 12, &deep_irq_ops, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    MR_TEST_ASSERT_EQ_INT(mr_irq_request(12, deep_irq_handler_none,
                                         deep_irq_owner(0), MR_IRQ_EDGE_RISING),
                          0);
    MR_TEST_ASSERT_EQ_INT(mr_irq_type(12), MR_IRQ_EDGE_RISING);
    MR_TEST_ASSERT_EQ_INT(deep_irq_last_type, MR_IRQ_EDGE_RISING);

    MR_TEST_ASSERT_EQ_INT(mr_irq_free(12, deep_irq_owner(0)), 0);
    MR_TEST_ASSERT_EQ_INT(mr_irq_request(12, deep_irq_handler_none,
                                         deep_irq_owner(0), MR_IRQ_LEVEL_LOW),
                          0);
    MR_TEST_ASSERT_EQ_INT(mr_irq_type(12), MR_IRQ_LEVEL_LOW);
    MR_TEST_ASSERT_EQ_INT(deep_irq_last_type, MR_IRQ_LEVEL_LOW);

    MR_TEST_ASSERT_EQ_INT(mr_irq_free(12, deep_irq_owner(0)), 0);
    MR_TEST_ASSERT_EQ_INT(mr_irq_del(&irq), 0);
}

/* Test: a handled return stops the shared chain, none runs all actions */
MR_TEST(irq, deep_handle_shared_shortcut) {
    mr_irq_t irq;
    mr_err_t ret;

    deep_irq_reset_ops();
    deep_irq_reset_handlers();

    ret = mr_irq_init(&irq, 13, 13, &deep_irq_ops, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(mr_irq_request(13, deep_irq_handler_none,
                                         deep_irq_owner(0),
                                         MR_IRQ_SHARED | MR_IRQ_EDGE_RISING),
                          0);
    MR_TEST_ASSERT_EQ_INT(mr_irq_request(13, deep_irq_handler_handled,
                                         deep_irq_owner(1),
                                         MR_IRQ_SHARED | MR_IRQ_EDGE_RISING),
                          0);
    MR_TEST_ASSERT_EQ_INT(mr_irq_request(13, deep_irq_handler_none,
                                         deep_irq_owner(2),
                                         MR_IRQ_SHARED | MR_IRQ_EDGE_RISING),
                          0);

    /* The handled action stops the chain before the third owner */
    MR_TEST_ASSERT_EQ_INT(mr_irq_handle(13), MR_IRQ_HANDLED);
    MR_TEST_ASSERT_EQ_INT(deep_irq_handler_order_n, 2);
    MR_TEST_ASSERT_EQ_INT(deep_irq_handler_order[0], 0);
    MR_TEST_ASSERT_EQ_INT(deep_irq_handler_order[1], 1);

    /* Free the shortcut owner so the remaining chain runs to the end */
    deep_irq_reset_handlers();
    MR_TEST_ASSERT_EQ_INT(mr_irq_free(13, deep_irq_owner(1)), 0);
    MR_TEST_ASSERT_EQ_INT(mr_irq_handle(13), MR_IRQ_NONE);
    MR_TEST_ASSERT_EQ_INT(deep_irq_handler_order_n, 2);
    MR_TEST_ASSERT_EQ_INT(deep_irq_handler_order[0], 0);
    MR_TEST_ASSERT_EQ_INT(deep_irq_handler_order[1], 2);

    MR_TEST_ASSERT_EQ_INT(mr_irq_free(13, deep_irq_owner(0)), 0);
    MR_TEST_ASSERT_EQ_INT(mr_irq_free(13, deep_irq_owner(2)), 0);
    MR_TEST_ASSERT_EQ_INT(mr_irq_del(&irq), 0);
}

/* Test: a disabled line still delivers on handle and stays masked */
MR_TEST(irq, deep_handle_mask_depth) {
    mr_irq_t irq;
    mr_err_t ret;

    deep_irq_reset_ops();
    deep_irq_reset_handlers();

    ret = mr_irq_init(&irq, 14, 14, &deep_irq_ops, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(mr_irq_request(14, deep_irq_handler_handled,
                                         deep_irq_owner(0), MR_IRQ_LEVEL_HIGH),
                          0);

    deep_irq_reset_ops();
    MR_TEST_ASSERT_EQ_INT(mr_irq_disable(14), 0);
    MR_TEST_ASSERT_EQ_INT(deep_irq_disable_count, 1);
    MR_TEST_ASSERT_EQ_INT(deep_irq_mask_count, 1);

    /* The software handle still runs the action but never unmasks */
    MR_TEST_ASSERT_EQ_INT(mr_irq_handle(14), MR_IRQ_HANDLED);
    MR_TEST_ASSERT_EQ_INT(deep_irq_handler_count[0], 1);
    MR_TEST_ASSERT_EQ_INT(deep_irq_mask_count, 2);
    MR_TEST_ASSERT_EQ_INT(deep_irq_unmask_count, 0);

    MR_TEST_ASSERT_EQ_INT(mr_irq_enable(14), 0);
    MR_TEST_ASSERT_EQ_INT(deep_irq_unmask_count, 1);
    MR_TEST_ASSERT_EQ_INT(deep_irq_enable_count, 1);

    /* An enabled line is masked and unmasked around the handle */
    deep_irq_reset_ops();
    MR_TEST_ASSERT_EQ_INT(mr_irq_handle(14), MR_IRQ_HANDLED);
    MR_TEST_ASSERT_EQ_INT(deep_irq_mask_count, 1);
    MR_TEST_ASSERT_EQ_INT(deep_irq_unmask_count, 1);

    MR_TEST_ASSERT_EQ_INT(mr_irq_free(14, deep_irq_owner(0)), 0);
    MR_TEST_ASSERT_EQ_INT(mr_irq_del(&irq), 0);
}

/* Test: repeated deferred edges before one execution coalesce into a
 * single defer run and requeue cleanly afterwards */
MR_TEST(irq, deep_defer_edge_coalesce) {
    mr_irq_t irq;
    mr_err_t ret;

    deep_irq_reset_ops();
    deep_irq_reset_special();

    ret = mr_irq_init(&irq, 9, 9, &deep_irq_ops, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_irq_defer_request(9, deep_irq_handler_deferred, deep_irq_owner(0),
                               MR_IRQ_EDGE_RISING, deep_irq_defer_entry);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    MR_TEST_ASSERT_EQ_INT(mr_irq_handle(9), MR_IRQ_DEFERRED);
    MR_TEST_ASSERT_EQ_INT(deep_irq_defer_main_count, 1);

    /* A second edge while the defer work is queued is not requeued */
    MR_TEST_ASSERT_EQ_INT(mr_irq_handle(9), MR_IRQ_DEFERRED);
    MR_TEST_ASSERT_EQ_INT(deep_irq_defer_main_count, 2);

    mr_irq_defer_execute();
    MR_TEST_ASSERT_EQ_INT(deep_irq_defer_count, 1);
    mr_irq_defer_execute();
    MR_TEST_ASSERT_EQ_INT(deep_irq_defer_count, 1);

    /* After the defer work drained the queue the line rearms again */
    MR_TEST_ASSERT_EQ_INT(mr_irq_handle(9), MR_IRQ_DEFERRED);
    mr_irq_defer_execute();
    MR_TEST_ASSERT_EQ_INT(deep_irq_defer_count, 2);
    MR_TEST_ASSERT_EQ_INT(deep_irq_defer_main_count, 3);

    MR_TEST_ASSERT_EQ_INT(mr_irq_free(9, deep_irq_owner(0)), 0);
    mr_irq_defer_execute();
    MR_TEST_ASSERT_EQ_INT(deep_irq_defer_count, 2);
    MR_TEST_ASSERT_EQ_INT(mr_irq_del(&irq), 0);
}

/* Test: a plain request treats a deferred return as handled */
MR_TEST(irq, deep_deferred_without_defer_entry) {
    mr_irq_t irq;
    mr_err_t ret;

    deep_irq_reset_ops();
    deep_irq_reset_special();

    ret = mr_irq_init(&irq, 8, 8, &deep_irq_ops, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(mr_irq_request(8, deep_irq_handler_deferred,
                                         deep_irq_owner(0), MR_IRQ_EDGE_RISING),
                          0);

    MR_TEST_ASSERT_EQ_INT(mr_irq_handle(8), MR_IRQ_HANDLED);
    MR_TEST_ASSERT_EQ_INT(deep_irq_defer_main_count, 1);
    mr_irq_defer_execute();
    MR_TEST_ASSERT_EQ_INT(deep_irq_defer_count, 0);

    MR_TEST_ASSERT_EQ_INT(mr_irq_free(8, deep_irq_owner(0)), 0);
    MR_TEST_ASSERT_EQ_INT(mr_irq_del(&irq), 0);
}

/* Test: a handler may trigger one nested handle of the same line */
MR_TEST(irq, deep_nested_handle_same_line) {
    mr_irq_t irq;
    mr_err_t ret;

    deep_irq_reset_ops();
    deep_irq_reset_special();

    ret = mr_irq_init(&irq, 7, 7, &deep_irq_ops, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(mr_irq_request(7, deep_irq_handler_nested,
                                         deep_irq_owner(0), MR_IRQ_LEVEL_HIGH),
                          0);

    deep_irq_reset_ops();
    MR_TEST_ASSERT_EQ_INT(mr_irq_handle(7), MR_IRQ_HANDLED);
    MR_TEST_ASSERT_EQ_INT(deep_irq_nested_depth, 2);
    MR_TEST_ASSERT_EQ_INT(deep_irq_nested_ret, MR_IRQ_HANDLED);
    MR_TEST_ASSERT_EQ_INT(deep_irq_mask_count, 2);
    MR_TEST_ASSERT_EQ_INT(deep_irq_unmask_count, 2);

    MR_TEST_ASSERT_EQ_INT(mr_irq_free(7, deep_irq_owner(0)), 0);
    MR_TEST_ASSERT_EQ_INT(mr_irq_del(&irq), 0);
}

/* Test: deleting the irq from within its own handler must not corrupt the
 * pending action bookkeeping */
MR_TEST(irq, deep_del_from_own_handler) {
    mr_irq_t irq;
    mr_err_t ret;

    deep_irq_reset_ops();
    deep_irq_reset_special();

    ret = mr_irq_init(&irq, 6, 6, &deep_irq_ops, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    deep_irq_del_target = &irq;
    MR_TEST_ASSERT_EQ_INT(mr_irq_request(6, deep_irq_handler_del,
                                         deep_irq_owner(0), MR_IRQ_LEVEL_HIGH),
                          0);

    deep_irq_reset_ops();
    MR_TEST_EXPECT_EQ_INT(mr_irq_handle(6), MR_IRQ_HANDLED);
    MR_TEST_ASSERT_EQ_INT(deep_irq_del_in_handler, 1);
    MR_TEST_ASSERT_EQ_INT(deep_irq_del_in_handler_ret, 0);
    MR_TEST_ASSERT_EQ_INT(deep_irq_disable_count, 1);
    /* The handle entry masks the line and the del path masks it again */
    MR_TEST_ASSERT_EQ_INT(deep_irq_mask_count, 2);

    /* The line is gone, software handles report none */
    MR_TEST_ASSERT_EQ_INT(mr_irq_handle(6), MR_IRQ_NONE);
    MR_TEST_ASSERT_EQ_INT(mr_irq_enable(6), -MR_ENOENT);
    MR_TEST_ASSERT_EQ_INT(mr_irq_free(6, deep_irq_owner(0)), -MR_ENOENT);
}

/* Test: freeing a middle shared action keeps the remaining ones working
 * and only the last free disables the line */
MR_TEST(irq, deep_free_middle_shared_depth) {
    mr_irq_t irq;
    mr_err_t ret;

    deep_irq_reset_ops();
    deep_irq_reset_handlers();

    ret = mr_irq_init(&irq, 3, 3, &deep_irq_ops, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(mr_irq_request(3, deep_irq_handler_none,
                                         deep_irq_owner(0),
                                         MR_IRQ_SHARED | MR_IRQ_LEVEL_HIGH), 0);
    MR_TEST_ASSERT_EQ_INT(mr_irq_request(3, deep_irq_handler_none,
                                         deep_irq_owner(1),
                                         MR_IRQ_SHARED | MR_IRQ_LEVEL_HIGH), 0);
    MR_TEST_ASSERT_EQ_INT(mr_irq_request(3, deep_irq_handler_none,
                                         deep_irq_owner(2),
                                         MR_IRQ_SHARED | MR_IRQ_LEVEL_HIGH), 0);

    /* No disable happens while valid owners remain */
    MR_TEST_ASSERT_EQ_INT(deep_irq_disable_count, 0);
    MR_TEST_ASSERT_EQ_INT(mr_irq_free(3, deep_irq_owner(1)), 0);
    MR_TEST_ASSERT_EQ_INT(deep_irq_disable_count, 0);

    MR_TEST_ASSERT_EQ_INT(mr_irq_handle(3), MR_IRQ_NONE);
    MR_TEST_ASSERT_EQ_INT(deep_irq_handler_order_n, 2);
    MR_TEST_ASSERT_EQ_INT(deep_irq_handler_order[0], 0);
    MR_TEST_ASSERT_EQ_INT(deep_irq_handler_order[1], 2);

    MR_TEST_ASSERT_EQ_INT(mr_irq_free(3, deep_irq_owner(0)), 0);
    MR_TEST_ASSERT_EQ_INT(deep_irq_disable_count, 0);

    /* The last free disables and masks the line, the earlier handle call
       also masked once */
    MR_TEST_ASSERT_EQ_INT(mr_irq_free(3, deep_irq_owner(2)), 0);
    MR_TEST_ASSERT_EQ_INT(deep_irq_disable_count, 1);
    MR_TEST_ASSERT_EQ_INT(deep_irq_mask_count, 2);

    deep_irq_reset_handlers();
    MR_TEST_ASSERT_EQ_INT(mr_irq_handle(3), MR_IRQ_NONE);
    MR_TEST_ASSERT_EQ_INT(deep_irq_handler_order_n, 0);

    MR_TEST_ASSERT_EQ_INT(mr_irq_del(&irq), 0);
}

/* Test: enable and disable calls without any request still balance the
 * hardware operations */
MR_TEST(irq, deep_enable_disable_without_action) {
    mr_irq_t irq;
    mr_err_t ret;

    deep_irq_reset_ops();
    deep_irq_reset_handlers();

    ret = mr_irq_init(&irq, 2, 2, &deep_irq_ops, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    /* Enable on a registered line with no action moves depth 1 to 0 */
    deep_irq_reset_ops();
    MR_TEST_ASSERT_EQ_INT(mr_irq_enable(2), 0);
    MR_TEST_ASSERT_EQ_INT(deep_irq_enable_count, 1);
    MR_TEST_ASSERT_EQ_INT(deep_irq_unmask_count, 1);

    MR_TEST_ASSERT_EQ_INT(mr_irq_disable(2), 0);
    MR_TEST_ASSERT_EQ_INT(deep_irq_disable_count, 1);
    MR_TEST_ASSERT_EQ_INT(deep_irq_mask_count, 1);

    /* A request on a disabled line enables it again */
    deep_irq_reset_ops();
    MR_TEST_ASSERT_EQ_INT(mr_irq_request(2, deep_irq_handler_handled,
                                         deep_irq_owner(0), MR_IRQ_LEVEL_HIGH),
                          0);
    MR_TEST_ASSERT_EQ_INT(deep_irq_enable_count, 1);
    MR_TEST_ASSERT_EQ_INT(deep_irq_unmask_count, 1);

    MR_TEST_ASSERT_EQ_INT(mr_irq_handle(2), MR_IRQ_HANDLED);
    MR_TEST_ASSERT_EQ_INT(deep_irq_handler_count[0], 1);
    MR_TEST_ASSERT_EQ_INT(mr_irq_free(2, deep_irq_owner(0)), 0);
    MR_TEST_ASSERT_EQ_INT(deep_irq_disable_count, 1);
    /* The earlier handle call masked the line once as well */
    MR_TEST_ASSERT_EQ_INT(deep_irq_mask_count, 2);

    MR_TEST_ASSERT_EQ_INT(mr_irq_del(&irq), 0);
}

/* Test: a dynamic controller spanning the last numbers of the table works
 * across its whole range */
MR_TEST(irq, deep_dynamic_range_upper_boundary) {
    mr_irq_t *irq;

    deep_irq_reset_ops();
    deep_irq_reset_handlers();

    irq = mr_irq_create(14, 15, &deep_irq_ops, MR_NULL);
    MR_TEST_ASSERT_NE_PTR(irq, MR_NULL);

    MR_TEST_ASSERT_EQ_INT(mr_irq_request(14, deep_irq_handler_handled,
                                         deep_irq_owner(0), MR_IRQ_EDGE_RISING),
                          0);
    MR_TEST_ASSERT_EQ_INT(mr_irq_request(15, deep_irq_handler_handled,
                                         deep_irq_owner(1), MR_IRQ_EDGE_FALLING),
                          0);

    MR_TEST_ASSERT_EQ_INT(mr_irq_handle(14), MR_IRQ_HANDLED);
    MR_TEST_ASSERT_EQ_INT(mr_irq_handle(15), MR_IRQ_HANDLED);
    MR_TEST_ASSERT_EQ_INT(deep_irq_handler_count[0], 1);
    MR_TEST_ASSERT_EQ_INT(deep_irq_handler_count[1], 1);

    MR_TEST_ASSERT_EQ_INT(mr_irq_free(14, deep_irq_owner(0)), 0);
    MR_TEST_ASSERT_EQ_INT(mr_irq_free(15, deep_irq_owner(1)), 0);
    MR_TEST_ASSERT_EQ_INT(mr_irq_del(irq), 0);
}

#endif /* defined(MR_USE_IRQ) */
#endif /* defined(MR_USE_TEST) */
