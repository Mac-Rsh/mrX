/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2026-08-30        MacRsh        Multi-thread scheduling deep test
 */

#include <mrX/test/mr_test.h>
#if defined(MR_USE_TEST)
#if defined(MR_USE_TEST_PTHREAD)
#include <pthread.h>
#include <sched.h>
#include <mrX/kernel/mr_work.h>
#include <mrX/kernel/mr_wait.h>
#include <mrX/kernel/mr_irq.h>
#include <mrX/libc/mr_atomic.h>
#include <mrX/libc/mr_string.h>

/* Multi-thread test thread count definition */
#define MT_SCHED_THREADS (3)

/* Multi-thread test per thread iteration count definition */
#define MT_SCHED_ITERS (100)

/* Multi-thread test total batch work count definition */
#define MT_WORK_COUNT (MT_SCHED_THREADS * MT_SCHED_ITERS)

/* Multi-thread test delayed per thread work count definition */
#define MT_DELAYED_ITERS (40)

/* Multi-thread test delayed total work count definition */
#define MT_DELAYED_COUNT (MT_SCHED_THREADS * MT_DELAYED_ITERS)

/* Multi-thread test wait register iteration count definition */
#define MT_REG_ITERS (50)

/* Multi-thread test spin safety limit definition */
#define MT_SCHED_SPIN_LIMIT (2000000)

/* Multi-thread test idle drain limit definition */
#define MT_SCHED_IDLE_LIMIT (100000)

/* Multi-thread thread handle type */
typedef struct mt_sched_thread {
    pthread_t handle;
    int started;
    int id;
} mt_sched_thread_t;

static void mt_sched_point(int counter) {
    if ((counter & 0xff) == 0) {
        sched_yield();
    }
}

static int mt_sched_join(mt_sched_thread_t *threads, int count) {
    int i;
    int ret = 0;

    for (i = 0; i < count; i++) {
        if (threads[i].started) {
            if (pthread_join(threads[i].handle, MR_NULL) != 0) {
                ret = -1;
            }
            threads[i].started = 0;
        }
    }
    return ret;
}

#if defined(MR_USE_WORK)

/* Multi-thread work batch item type */
typedef struct mt_work_item {
    struct mt_work_ctx *ctx;
    int tag;
} mt_work_item_t;

/* Multi-thread work context type */
typedef struct mt_work_ctx {
    mr_workqueue_t queue;
    mr_atomic_t scheduled;
    mr_atomic_t executed;
    mr_atomic_t done;
    mr_uint8_t fired[MT_WORK_COUNT];
    mr_uint8_t cancelled[MT_WORK_COUNT];
    mr_work_t storage[MT_WORK_COUNT];
    mr_work_t *works[MT_WORK_COUNT];
    mr_work_t single;
    mt_work_item_t items[MT_WORK_COUNT];
} mt_work_ctx_t;

/* Multi-thread work worker argument type */
typedef struct mt_work_arg {
    mt_work_ctx_t *ctx;
    int id;
    int count;
} mt_work_arg_t;

static void mt_work_batch_entry(struct mr_work *work, void *param) {
    mt_work_item_t *item = (mt_work_item_t *)param;
    mt_work_ctx_t *ctx = item->ctx;

    MR_IGNORE(work);
    if ((item->tag >= 0) && (item->tag < MT_WORK_COUNT)) {
        ctx->fired[item->tag] += 1;
    }
    mr_atomic_fetch_add(&ctx->executed, 1);
}

static void mt_work_single_entry(struct mr_work *work, void *param) {
    mt_work_ctx_t *ctx = (mt_work_ctx_t *)param;

    MR_IGNORE(work);
    mr_atomic_fetch_add(&ctx->executed, 1);
}

static void *mt_work_executor(void *argument) {
    mt_work_arg_t *arg = argument;
    mt_work_ctx_t *ctx = arg->ctx;
    int idle = 0;
    int spins = 0;

    while (spins < MT_SCHED_SPIN_LIMIT) {
        mr_workqueue_execute(&ctx->queue);
        mt_sched_point(spins);
        spins++;
        if (mr_atomic_load(&ctx->done) >= (mr_uint32_t)arg->count) {
            if (arg->id >= 0) {
                if ((int)mr_atomic_load(&ctx->executed) >= arg->id) {
                    break;
                }
            } else {
                idle++;
                if (idle > MT_SCHED_IDLE_LIMIT) {
                    break;
                }
            }
        }
    }
    mr_atomic_fetch_add(&ctx->done, 1);
    return MR_NULL;
}

static void *mt_work_producer(void *argument) {
    mt_work_arg_t *arg = argument;
    mt_work_ctx_t *ctx = arg->ctx;
    int base = arg->id * arg->count;
    int i;

    for (i = 0; i < arg->count; i++) {
        int tag = base + i;
        mr_work_t *work = mr_work_create(mt_work_batch_entry, &ctx->items[tag]);

        if (!work) {
            break;
        }
        ctx->works[tag] = work;
        if ((mr_work_bind(work, &ctx->queue) != 0)
            || (mr_work_schedule(work) != 0)) {
            break;
        }
        mr_atomic_fetch_add(&ctx->scheduled, 1);
        mt_sched_point(i);
    }
    mr_atomic_fetch_add(&ctx->done, 1);
    return MR_NULL;
}

static void *mt_work_statik_producer(void *argument) {
    mt_work_arg_t *arg = argument;
    mt_work_ctx_t *ctx = arg->ctx;
    int base = arg->id * arg->count;
    int i;

    for (i = 0; i < arg->count; i++) {
        int tag = base + i;

        if (mr_work_schedule(&ctx->storage[tag]) != 0) {
            break;
        }
        mr_atomic_fetch_add(&ctx->scheduled, 1);
        mt_sched_point(i);
    }
    mr_atomic_fetch_add(&ctx->done, 1);
    return MR_NULL;
}

static void *mt_work_delayed_producer(void *argument) {
    mt_work_arg_t *arg = argument;
    mt_work_ctx_t *ctx = arg->ctx;
    int base = arg->id * arg->count;
    int i;

    for (i = 0; i < arg->count; i++) {
        int tag = base + i;

        if (mr_work_delayed_schedule(&ctx->storage[tag], 2) != 0) {
            break;
        }
        mr_atomic_fetch_add(&ctx->scheduled, 1);
        mt_sched_point(i);
    }
    mr_atomic_fetch_add(&ctx->done, 1);
    return MR_NULL;
}

static void *mt_work_contender(void *argument) {
    mt_work_arg_t *arg = argument;
    mt_work_ctx_t *ctx = arg->ctx;
    int i;

    for (i = 0; i < arg->count; i++) {
        if (mr_work_schedule(&ctx->single) == 0) {
            mr_atomic_fetch_add(&ctx->scheduled, 1);
        }
        mt_sched_point(i);
    }
    mr_atomic_fetch_add(&ctx->done, 1);
    return MR_NULL;
}

static void *mt_work_canceller(void *argument) {
    mt_work_arg_t *arg = argument;
    mt_work_ctx_t *ctx = arg->ctx;
    int i;

    for (i = 0; i < arg->count; i += 3) {
        mr_work_cancel(&ctx->storage[i]);
        ctx->cancelled[i] = 1;
        mt_sched_point(i);
    }
    mr_atomic_fetch_add(&ctx->done, 1);
    return MR_NULL;
}

MR_TEST(work, mt_producer_executor_conservation) {
    mt_work_arg_t args[MT_SCHED_THREADS + 1];
    mt_sched_thread_t threads[MT_SCHED_THREADS + 1];
    /* Static so the tick thread timer callback never touches
     * reused stack memory after the case returns */
    static mt_work_ctx_t ctx;
    int spins = 0;
    int i;
    int ret;

    mr_memset(&ctx, 0, sizeof(ctx));
    for (i = 0; i < MT_WORK_COUNT; i++) {
        ctx.items[i].ctx = &ctx;
        ctx.items[i].tag = i;
    }
    ret = mr_workqueue_init(&ctx.queue);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    for (i = 0; i < MT_SCHED_THREADS; i++) {
        args[i].ctx = &ctx;
        args[i].id = i;
        args[i].count = MT_SCHED_ITERS;
        threads[i].started = 0;
        threads[i].id = i;
        ret = pthread_create(&threads[i].handle, MR_NULL, mt_work_producer,
                             &args[i]);
        MR_TEST_EXPECT_EQ_INT(ret, 0);
        if (ret != 0) {
            break;
        }
        threads[i].started = 1;
    }
    args[MT_SCHED_THREADS].ctx = &ctx;
    args[MT_SCHED_THREADS].id = MT_WORK_COUNT;
    args[MT_SCHED_THREADS].count = MT_SCHED_THREADS;
    threads[MT_SCHED_THREADS].started = 0;
    threads[MT_SCHED_THREADS].id = -1;
    ret = pthread_create(&threads[MT_SCHED_THREADS].handle, MR_NULL,
                         mt_work_executor, &args[MT_SCHED_THREADS]);
    if (ret == 0) {
        threads[MT_SCHED_THREADS].started = 1;
    }
    MR_TEST_EXPECT_EQ_INT(mt_sched_join(threads, MT_SCHED_THREADS), 0);
    MR_TEST_EXPECT_EQ_INT(
        mt_sched_join(&threads[MT_SCHED_THREADS],
                      threads[MT_SCHED_THREADS].started ? 1 : 0),
        0);

    /* Final drain on the main thread in case the executor left early */
    while ((spins < 1000)
           && ((int)mr_atomic_load(&ctx.executed)
               < (int)mr_atomic_load(&ctx.scheduled))) {
        mr_workqueue_execute(&ctx.queue);
        spins++;
    }

    /* Every scheduled work must have executed exactly once */
    MR_TEST_EXPECT_EQ_UINT((mr_uint32_t)mr_atomic_load(&ctx.scheduled),
                           (mr_uint32_t)MT_WORK_COUNT);
    MR_TEST_EXPECT_EQ_UINT((mr_uint32_t)mr_atomic_load(&ctx.executed),
                           (mr_uint32_t)mr_atomic_load(&ctx.scheduled));
    for (i = 0; i < MT_WORK_COUNT; i++) {
        if (ctx.fired[i] != 1) {
            MR_TEST_EXPECT_EQ_INT(ctx.fired[i], 1);
            break;
        }
    }

    /* Delete the dynamic works only after the executor has been joined */
    for (i = 0; i < MT_WORK_COUNT; i++) {
        if (ctx.works[i]) {
            MR_TEST_EXPECT_EQ_INT(mr_work_del(ctx.works[i]), 0);
        }
    }
    MR_TEST_EXPECT_EQ_INT(mr_workqueue_del(&ctx.queue), 0);
    /* Note: deleting a dynamic work while an executor may still dispatch
     * its entry is inherently racy(use after free) and is deliberately
     * not exercised here */
}

MR_TEST(work, mt_cancel_vs_execute_race) {
    mt_work_arg_t args[MT_SCHED_THREADS + 1];
    mt_sched_thread_t threads[MT_SCHED_THREADS + 1];
    /* Static so the tick thread timer callback never touches
     * reused stack memory after the case returns */
    static mt_work_ctx_t ctx;
    int producers = 2;
    int per_producer = 100;
    unsigned int fired_total = 0;
    int i;
    int ret;

    mr_memset(&ctx, 0, sizeof(ctx));
    ret = mr_workqueue_init(&ctx.queue);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    for (i = 0; i < producers * per_producer; i++) {
        ctx.items[i].ctx = &ctx;
        ctx.items[i].tag = i;
        ret = mr_work_init(&ctx.storage[i], mt_work_batch_entry, &ctx.items[i]);
        MR_TEST_ASSERT_EQ_INT(ret, 0);
        MR_TEST_ASSERT_EQ_INT(mr_work_bind(&ctx.storage[i], &ctx.queue), 0);
    }

    for (i = 0; i < producers; i++) {
        args[i].ctx = &ctx;
        args[i].id = i;
        args[i].count = per_producer;
        threads[i].started = 0;
        threads[i].id = i;
        ret = pthread_create(&threads[i].handle, MR_NULL,
                             mt_work_statik_producer, &args[i]);
        MR_TEST_EXPECT_EQ_INT(ret, 0);
        if (ret != 0) {
            break;
        }
        threads[i].started = 1;
    }
    args[producers].ctx = &ctx;
    args[producers].id = 0;
    args[producers].count = producers * per_producer;
    threads[producers].started = 0;
    threads[producers].id = -1;
    ret = pthread_create(&threads[producers].handle, MR_NULL, mt_work_canceller,
                         &args[producers]);
    if (ret == 0) {
        threads[producers].started = 1;
    }
    args[producers + 1].ctx = &ctx;
    args[producers + 1].id = -1;
    args[producers + 1].count = producers + 1;
    threads[producers + 1].started = 0;
    threads[producers + 1].id = -1;
    ret = pthread_create(&threads[producers + 1].handle, MR_NULL,
                         mt_work_executor, &args[producers + 1]);
    if (ret == 0) {
        threads[producers + 1].started = 1;
    }
    MR_TEST_EXPECT_EQ_INT(mt_sched_join(threads, producers + 2), 0);

    /* Final flush so every uncancelled scheduled work has been run */
    for (i = 0; i < 4; i++) {
        mr_workqueue_execute(&ctx.queue);
    }

    /* A cancelled work may run zero times(raced before pop) or once
     * (already popped) but never more than once */
    for (i = 0; i < producers * per_producer; i++) {
        if (ctx.fired[i] > 1) {
            MR_TEST_EXPECT_LE_UINT(ctx.fired[i], 1);
            break;
        }
    }
    for (i = 0; i < producers * per_producer; i++) {
        fired_total += ctx.fired[i];
    }
    MR_TEST_EXPECT_LE_UINT(fired_total,
                           (mr_uint32_t)(producers * per_producer));
    MR_TEST_EXPECT_EQ_UINT(fired_total,
                           (mr_uint32_t)mr_atomic_load(&ctx.executed));

    for (i = 0; i < producers * per_producer; i++) {
        MR_TEST_EXPECT_EQ_INT(mr_work_del(&ctx.storage[i]), 0);
    }
    MR_TEST_EXPECT_EQ_INT(mr_workqueue_del(&ctx.queue), 0);
}

MR_TEST(work, mt_delayed_concurrent_fire) {
    mt_work_arg_t args[MT_SCHED_THREADS + 1];
    mt_sched_thread_t threads[MT_SCHED_THREADS + 1];
    /* Static so the tick thread timer callback never touches
     * reused stack memory after the case returns */
    static mt_work_ctx_t ctx;
    int i;
    int ret;

    mr_memset(&ctx, 0, sizeof(ctx));
    ret = mr_workqueue_init(&ctx.queue);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    for (i = 0; i < MT_DELAYED_COUNT; i++) {
        ctx.items[i].ctx = &ctx;
        ctx.items[i].tag = i;
        ret = mr_work_init(&ctx.storage[i], mt_work_batch_entry, &ctx.items[i]);
        MR_TEST_ASSERT_EQ_INT(ret, 0);
        MR_TEST_ASSERT_EQ_INT(mr_work_bind(&ctx.storage[i], &ctx.queue), 0);
    }

    for (i = 0; i < MT_SCHED_THREADS; i++) {
        args[i].ctx = &ctx;
        args[i].id = i;
        args[i].count = MT_DELAYED_ITERS;
        threads[i].started = 0;
        threads[i].id = i;
        ret = pthread_create(&threads[i].handle, MR_NULL,
                             mt_work_delayed_producer, &args[i]);
        MR_TEST_EXPECT_EQ_INT(ret, 0);
        if (ret != 0) {
            break;
        }
        threads[i].started = 1;
    }
    args[MT_SCHED_THREADS].ctx = &ctx;
    args[MT_SCHED_THREADS].id = MT_DELAYED_COUNT;
    args[MT_SCHED_THREADS].count = MT_SCHED_THREADS;
    threads[MT_SCHED_THREADS].started = 0;
    threads[MT_SCHED_THREADS].id = -1;
    ret = pthread_create(&threads[MT_SCHED_THREADS].handle, MR_NULL,
                         mt_work_executor, &args[MT_SCHED_THREADS]);
    if (ret == 0) {
        threads[MT_SCHED_THREADS].started = 1;
    }
    MR_TEST_EXPECT_EQ_INT(mt_sched_join(threads, MT_SCHED_THREADS), 0);
    MR_TEST_EXPECT_EQ_INT(
        mt_sched_join(&threads[MT_SCHED_THREADS],
                      threads[MT_SCHED_THREADS].started ? 1 : 0),
        0);

    /* The realtime tick thread advances the clock, the executor drains
     * the queue as the delayed works expire */
    MR_TEST_EXPECT_EQ_UINT((mr_uint32_t)mr_atomic_load(&ctx.executed),
                           (mr_uint32_t)MT_DELAYED_COUNT);
    for (i = 0; i < MT_DELAYED_COUNT; i++) {
        if (ctx.fired[i] != 1) {
            MR_TEST_EXPECT_EQ_INT(ctx.fired[i], 1);
            break;
        }
    }

    for (i = 0; i < MT_DELAYED_COUNT; i++) {
        MR_TEST_EXPECT_EQ_INT(mr_work_del(&ctx.storage[i]), 0);
    }
    MR_TEST_EXPECT_EQ_INT(mr_workqueue_del(&ctx.queue), 0);
}

MR_TEST(work, mt_single_work_contended_schedule) {
    mt_work_arg_t args[MT_SCHED_THREADS + 1];
    mt_sched_thread_t threads[MT_SCHED_THREADS + 1];
    /* Static so the tick thread timer callback never touches
     * reused stack memory after the case returns */
    static mt_work_ctx_t ctx;
    int i;
    int ret;

    mr_memset(&ctx, 0, sizeof(ctx));
    ret = mr_workqueue_init(&ctx.queue);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_work_init(&ctx.single, mt_work_single_entry, &ctx);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(mr_work_bind(&ctx.single, &ctx.queue), 0);

    for (i = 0; i < MT_SCHED_THREADS; i++) {
        args[i].ctx = &ctx;
        args[i].id = i;
        args[i].count = 200;
        threads[i].started = 0;
        threads[i].id = i;
        ret = pthread_create(&threads[i].handle, MR_NULL, mt_work_contender,
                             &args[i]);
        MR_TEST_EXPECT_EQ_INT(ret, 0);
        if (ret != 0) {
            break;
        }
        threads[i].started = 1;
    }
    args[MT_SCHED_THREADS].ctx = &ctx;
    args[MT_SCHED_THREADS].id = -1;
    args[MT_SCHED_THREADS].count = MT_SCHED_THREADS;
    threads[MT_SCHED_THREADS].started = 0;
    threads[MT_SCHED_THREADS].id = -1;
    ret = pthread_create(&threads[MT_SCHED_THREADS].handle, MR_NULL,
                         mt_work_executor, &args[MT_SCHED_THREADS]);
    if (ret == 0) {
        threads[MT_SCHED_THREADS].started = 1;
    }
    MR_TEST_EXPECT_EQ_INT(mt_sched_join(threads, MT_SCHED_THREADS), 0);
    MR_TEST_EXPECT_EQ_INT(
        mt_sched_join(&threads[MT_SCHED_THREADS],
                      threads[MT_SCHED_THREADS].started ? 1 : 0),
        0);

    for (i = 0; i < 4; i++) {
        mr_workqueue_execute(&ctx.queue);
    }

    /* Every successful schedule must produce exactly one execution */
    MR_TEST_EXPECT_EQ_UINT((mr_uint32_t)mr_atomic_load(&ctx.executed),
                           (mr_uint32_t)mr_atomic_load(&ctx.scheduled));
    MR_TEST_EXPECT_LE_UINT((mr_uint32_t)mr_atomic_load(&ctx.scheduled),
                           (mr_uint32_t)(MT_SCHED_THREADS * 200));
    MR_TEST_EXPECT_EQ_INT(MR_WORK_IS_RUNNING(&ctx.single), MR_FALSE);

    MR_TEST_EXPECT_EQ_INT(mr_work_del(&ctx.single), 0);
    MR_TEST_EXPECT_EQ_INT(mr_workqueue_del(&ctx.queue), 0);
}

#endif /* defined(MR_USE_WORK) */

#if defined(MR_USE_WAIT)

/* Multi-thread wait context type */
typedef struct mt_wait_ctx {
    mr_waitable_t able;
    mr_waiter_t waiters[MT_SCHED_THREADS];
    mr_atomic_t fires[MT_SCHED_THREADS];
    mr_atomic_t registered;
    int inited[MT_SCHED_THREADS];
} mt_wait_ctx_t;

/* Multi-thread wait callback argument type */
typedef struct mt_wait_arg {
    mt_wait_ctx_t *ctx;
    int id;
} mt_wait_arg_t;

static void mt_wait_cb(mr_waiter_t *waiter, void *param, mr_wait_event_t event,
                       const void *data) {
    mt_wait_arg_t *arg = (mt_wait_arg_t *)param;

    MR_IGNORE(waiter);
    MR_IGNORE(event);
    MR_IGNORE(data);
    mr_atomic_fetch_add(&arg->ctx->fires[arg->id], 1);
}

static void *mt_wait_register_thread(void *argument) {
    mt_wait_arg_t *arg = argument;
    mt_wait_ctx_t *ctx = arg->ctx;
    int id = arg->id;
    int spins = 0;

    if (mr_waiter_init(&ctx->waiters[id], mt_wait_cb, arg, MR_WAIT_IN) != 0) {
        return MR_NULL;
    }
    ctx->inited[id] = 1;
    if (mr_waiter_wait(&ctx->waiters[id], &ctx->able) != 0) {
        return MR_NULL;
    }
    mr_atomic_fetch_add(&ctx->registered, 1);
    while ((spins < MT_SCHED_SPIN_LIMIT)
           && (mr_atomic_load(&ctx->fires[id]) == 0)) {
        spins++;
        mt_sched_point(spins);
    }
    return MR_NULL;
}

MR_TEST(wait, mt_registered_waiters_single_raise) {
    mt_wait_arg_t args[MT_SCHED_THREADS];
    mt_sched_thread_t threads[MT_SCHED_THREADS];
    mt_wait_ctx_t ctx;
    int created = 0;
    int spins = 0;
    int i;
    int ret;

    mr_memset(&ctx, 0, sizeof(ctx));
    ret = mr_waitable_init(&ctx.able);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    for (i = 0; i < MT_SCHED_THREADS; i++) {
        args[i].ctx = &ctx;
        args[i].id = i;
        threads[i].started = 0;
        threads[i].id = i;
        ret = pthread_create(&threads[i].handle, MR_NULL,
                             mt_wait_register_thread, &args[i]);
        MR_TEST_EXPECT_EQ_INT(ret, 0);
        if (ret != 0) {
            break;
        }
        threads[i].started = 1;
        created++;
    }
    while ((spins < MT_SCHED_SPIN_LIMIT)
           && ((int)mr_atomic_load(&ctx.registered) < created)) {
        spins++;
    }
    ret = mr_waitable_raise(&ctx.able, MR_WAIT_IN, MR_NULL);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    MR_TEST_EXPECT_EQ_INT(mt_sched_join(threads, MT_SCHED_THREADS), 0);

    /* Every registered waiter must have fired exactly once */
    MR_TEST_EXPECT_EQ_UINT((mr_uint32_t)mr_atomic_load(&ctx.registered),
                           (mr_uint32_t)created);
    for (i = 0; i < created; i++) {
        MR_TEST_EXPECT_EQ_UINT((mr_uint32_t)mr_atomic_load(&ctx.fires[i]), 1);
    }

    for (i = 0; i < MT_SCHED_THREADS; i++) {
        if (ctx.inited[i]) {
            MR_TEST_EXPECT_EQ_INT(mr_waiter_del(&ctx.waiters[i]), 0);
        }
    }
    MR_TEST_EXPECT_EQ_INT(mr_waitable_del(&ctx.able), 0);
}

/* Multi-thread register-during-raise context type */
typedef struct mt_reg_ctx {
    mr_waitable_t able;
    mr_waiter_t waiters[MT_REG_ITERS];
    struct mt_reg_item {
        struct mt_reg_ctx *ctx;
        int id;
    } args[MT_REG_ITERS];
    mr_atomic_t fires[MT_REG_ITERS];
    mr_atomic_t stop;
    mr_uint8_t fired_seen[MT_REG_ITERS];
    int inited[MT_REG_ITERS];
    int reg_fail;
} mt_reg_ctx_t;

typedef struct mt_reg_arg {
    mt_reg_ctx_t *ctx;
    int id;
} mt_reg_arg_outer_t;

static void mt_reg_cb(mr_waiter_t *waiter, void *param, mr_wait_event_t event,
                      const void *data) {
    struct mt_reg_item *arg = (struct mt_reg_item *)param;

    MR_IGNORE(waiter);
    MR_IGNORE(event);
    MR_IGNORE(data);
    mr_atomic_fetch_add(&arg->ctx->fires[arg->id], 1);
}

static void *mt_reg_raiser(void *argument) {
    mt_reg_arg_outer_t *arg = argument;
    mt_reg_ctx_t *ctx = arg->ctx;
    int i = 0;

    while ((i < MT_SCHED_SPIN_LIMIT) && (mr_atomic_load(&ctx->stop) == 0)) {
        mr_waitable_raise(&ctx->able, MR_WAIT_IN, MR_NULL);
        mt_sched_point(i);
        i++;
    }
    return MR_NULL;
}

static void *mt_reg_registerer(void *argument) {
    mt_reg_arg_outer_t *arg = argument;
    mt_reg_ctx_t *ctx = arg->ctx;
    int i;
    int spins;

    for (i = 0; i < MT_REG_ITERS; i++) {
        if (mr_waiter_init(&ctx->waiters[i], mt_reg_cb, &ctx->args[i],
                           MR_WAIT_IN)
            != 0) {
            ctx->reg_fail = 1;
            break;
        }
        ctx->inited[i] = 1;
        if (mr_waiter_wait(&ctx->waiters[i], &ctx->able) != 0) {
            ctx->reg_fail = 1;
            mr_waiter_cancel(&ctx->waiters[i]);
            break;
        }

        /* The waiter must fire on this raise or the next one */
        spins = 0;
        while ((spins < MT_SCHED_SPIN_LIMIT)
               && (mr_atomic_load(&ctx->fires[i]) == 0)) {
            spins++;
            mt_sched_point(spins);
        }
        if (mr_atomic_load(&ctx->fires[i]) > 0) {
            ctx->fired_seen[i] = 1;
        }
        mr_waiter_cancel(&ctx->waiters[i]);
    }
    mr_atomic_store(&ctx->stop, 1);
    return MR_NULL;
}

MR_TEST(wait, mt_register_during_raise) {
    mt_reg_arg_outer_t rarg;
    mt_reg_arg_outer_t larg;
    mt_sched_thread_t threads[2];
    mt_reg_ctx_t ctx;
    int i;
    int ret;

    mr_memset(&ctx, 0, sizeof(ctx));
    ret = mr_waitable_init(&ctx.able);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    for (i = 0; i < MT_REG_ITERS; i++) {
        ctx.args[i].ctx = &ctx;
        ctx.args[i].id = i;
    }
    larg.ctx = &ctx;
    larg.id = -1;
    rarg.ctx = &ctx;
    rarg.id = -1;

    threads[0].started = 0;
    threads[0].id = 0;
    ret = pthread_create(&threads[0].handle, MR_NULL, mt_reg_raiser, &larg);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret == 0) {
        threads[0].started = 1;
    }
    threads[1].started = 0;
    threads[1].id = 1;
    ret = pthread_create(&threads[1].handle, MR_NULL, mt_reg_registerer, &rarg);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret == 0) {
        threads[1].started = 1;
    }
    MR_TEST_EXPECT_EQ_INT(mt_sched_join(threads, 2), 0);

    /* Every waiter registered mid raise must have fired at least once */
    MR_TEST_EXPECT_EQ_INT(ctx.reg_fail, 0);
    for (i = 0; i < MT_REG_ITERS; i++) {
        if (ctx.fired_seen[i] != 1) {
            MR_TEST_EXPECT_EQ_INT(ctx.fired_seen[i], 1);
            break;
        }
    }

    for (i = 0; i < MT_REG_ITERS; i++) {
        if (ctx.inited[i]) {
            /* Already cancelled waiters are removed harmlessly */
            MR_TEST_EXPECT_EQ_INT(mr_waiter_cancel(&ctx.waiters[i]), 0);
            MR_TEST_EXPECT_EQ_INT(mr_waiter_del(&ctx.waiters[i]), 0);
        }
    }
    MR_TEST_EXPECT_EQ_INT(mr_waitable_del(&ctx.able), 0);
}

/* Multi-thread event set versus cancel context type */
typedef struct mt_race_ctx {
    mr_waitable_t able;
    mr_waiter_t racer;
    mr_atomic_t fires;
    mr_atomic_t done;
    mr_atomic_t stop;
} mt_race_ctx_t;

static void mt_race_cb(mr_waiter_t *waiter, void *param, mr_wait_event_t event,
                       const void *data) {
    mt_race_ctx_t *ctx = (mt_race_ctx_t *)param;

    MR_IGNORE(waiter);
    MR_IGNORE(event);
    MR_IGNORE(data);
    mr_atomic_fetch_add(&ctx->fires, 1);
}

static void *mt_race_event_set(void *argument) {
    mt_race_ctx_t *ctx = argument;
    int i;

    for (i = 0; i < 200; i++) {
        mr_waiter_event_set(&ctx->racer, MR_WAIT_IN);
        mt_sched_point(i);
    }
    mr_atomic_fetch_add(&ctx->done, 1);
    return MR_NULL;
}

static void *mt_race_rewait(void *argument) {
    mt_race_ctx_t *ctx = argument;
    int i;

    for (i = 0; i < 200; i++) {
        mr_waiter_wait(&ctx->racer, &ctx->able);
        mt_sched_point(i);
    }
    mr_atomic_fetch_add(&ctx->done, 1);
    return MR_NULL;
}

static void *mt_race_raiser(void *argument) {
    mt_race_ctx_t *ctx = argument;
    int i = 0;

    while ((i < MT_SCHED_SPIN_LIMIT)
           && ((mr_atomic_load(&ctx->stop) == 0)
               && (mr_atomic_load(&ctx->done) < 2))) {
        mr_waitable_raise(&ctx->able, MR_WAIT_IN, MR_NULL);
        mt_sched_point(i);
        i++;
    }
    return MR_NULL;
}

MR_TEST(wait, mt_event_set_cancel_vs_raise) {
    mt_sched_thread_t threads[3];
    mt_race_ctx_t ctx;
    unsigned int fires_before;
    int spins = 0;
    int ret;

    mr_memset(&ctx, 0, sizeof(ctx));
    ret = mr_waitable_init(&ctx.able);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_waiter_init(&ctx.racer, mt_race_cb, &ctx, MR_WAIT_IN);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(mr_waiter_wait(&ctx.racer, &ctx.able), 0);

    threads[0].started = 0;
    threads[0].id = 0;
    ret = pthread_create(&threads[0].handle, MR_NULL, mt_race_event_set, &ctx);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret == 0) {
        threads[0].started = 1;
    }
    threads[1].started = 0;
    threads[1].id = 1;
    ret = pthread_create(&threads[1].handle, MR_NULL, mt_race_rewait, &ctx);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret == 0) {
        threads[1].started = 1;
    }
    threads[2].started = 0;
    threads[2].id = 2;
    ret = pthread_create(&threads[2].handle, MR_NULL, mt_race_raiser, &ctx);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret == 0) {
        threads[2].started = 1;
    }

    while ((spins < MT_SCHED_SPIN_LIMIT)
           && ((int)mr_atomic_load(&ctx.done) < 2)) {
        spins++;
        mt_sched_point(spins);
    }
    mr_atomic_store(&ctx.stop, 1);
    MR_TEST_EXPECT_EQ_INT(mt_sched_join(threads, 3), 0);

    /* The waiter must still be fully usable after the race */
    fires_before = (unsigned int)mr_atomic_load(&ctx.fires);
    MR_TEST_EXPECT_EQ_INT(mr_waiter_cancel(&ctx.racer), 0);
    ret = mr_waiter_wait(&ctx.racer, &ctx.able);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    ret = mr_waitable_raise(&ctx.able, MR_WAIT_IN, MR_NULL);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    MR_TEST_EXPECT_EQ_UINT((mr_uint32_t)mr_atomic_load(&ctx.fires),
                           fires_before + 1);

    MR_TEST_EXPECT_EQ_INT(mr_waiter_del(&ctx.racer), 0);
    MR_TEST_EXPECT_EQ_INT(mr_waitable_del(&ctx.able), 0);
}

/* Multi-thread cross raise context type */
typedef struct mt_cross_ctx {
    mr_waitable_t able1;
    mr_waitable_t able2;
    mr_waiter_t waiter1;
    mr_waiter_t waiter2;
    int fires1;
    int fires2;
} mt_cross_ctx_t;

static void mt_cross_cb1(mr_waiter_t *waiter, void *param,
                         mr_wait_event_t event, const void *data) {
    mt_cross_ctx_t *ctx = (mt_cross_ctx_t *)param;

    MR_IGNORE(waiter);
    MR_IGNORE(event);
    MR_IGNORE(data);
    ctx->fires1++;
}

static void mt_cross_cb2(mr_waiter_t *waiter, void *param,
                         mr_wait_event_t event, const void *data) {
    mt_cross_ctx_t *ctx = (mt_cross_ctx_t *)param;

    MR_IGNORE(waiter);
    MR_IGNORE(event);
    MR_IGNORE(data);
    ctx->fires2++;
}

/* Cross raise thread argument type */
typedef struct mt_cross_arg {
    mr_waitable_t *able;
    int count;
} mt_cross_arg_t;

static void *mt_cross_raiser(void *argument) {
    mt_cross_arg_t *arg = argument;
    int i;

    for (i = 0; i < arg->count; i++) {
        mr_waitable_raise(arg->able, MR_WAIT_IN, MR_NULL);
        mt_sched_point(i);
    }
    return MR_NULL;
}

MR_TEST(wait, mt_two_waitables_cross_raise) {
    mt_cross_arg_t args[2];
    mt_sched_thread_t threads[2];
    mt_cross_ctx_t ctx;
    int i;
    int ret;

    mr_memset(&ctx, 0, sizeof(ctx));
    ret = mr_waitable_init(&ctx.able1);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_waitable_init(&ctx.able2);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_waiter_init(&ctx.waiter1, mt_cross_cb1, &ctx, MR_WAIT_IN);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_waiter_init(&ctx.waiter2, mt_cross_cb2, &ctx, MR_WAIT_IN);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(mr_waiter_wait(&ctx.waiter1, &ctx.able1), 0);
    MR_TEST_ASSERT_EQ_INT(mr_waiter_wait(&ctx.waiter2, &ctx.able2), 0);

    args[0].able = &ctx.able1;
    args[0].count = 100;
    args[1].able = &ctx.able2;
    args[1].count = 100;
    for (i = 0; i < 2; i++) {
        threads[i].started = 0;
        threads[i].id = i;
        ret = pthread_create(&threads[i].handle, MR_NULL, mt_cross_raiser,
                             &args[i]);
        MR_TEST_EXPECT_EQ_INT(ret, 0);
        if (ret != 0) {
            break;
        }
        threads[i].started = 1;
    }
    MR_TEST_EXPECT_EQ_INT(mt_sched_join(threads, 2), 0);

    /* No list mutation happens during the raises so each waiter fires
     * exactly once per raise of its own waitable */
    MR_TEST_EXPECT_EQ_INT(ctx.fires1, 100);
    MR_TEST_EXPECT_EQ_INT(ctx.fires2, 100);

    MR_TEST_EXPECT_EQ_INT(mr_waiter_del(&ctx.waiter1), 0);
    MR_TEST_EXPECT_EQ_INT(mr_waiter_del(&ctx.waiter2), 0);
    MR_TEST_EXPECT_EQ_INT(mr_waitable_del(&ctx.able1), 0);
    MR_TEST_EXPECT_EQ_INT(mr_waitable_del(&ctx.able2), 0);
}

#endif /* defined(MR_USE_WAIT) */

#if defined(MR_USE_IRQ)
#if defined(MR_USE_IRQ_DEFER)

/* Irq owner identity objects */
static int mt_irq_owner1;
static int mt_irq_owner2;

/* Multi-thread irq context type */
typedef struct mt_irq_ctx {
    mr_irq_t irq;
    mr_atomic_t main_count;
    mr_atomic_t main2_count;
    mr_atomic_t fires;
    mr_atomic_t fires2;
    mr_atomic_t handles_done;
} mt_irq_ctx_t;

/* Active context pointer(set once per case before the threads start) */
static mt_irq_ctx_t *mt_irq_active = MR_NULL;

static void mt_irq_op_priority_set(mr_uint32_t irq, mr_uint8_t priority,
                                   void *priv) {
    MR_IGNORE(irq);
    MR_IGNORE(priority);
    MR_IGNORE(priv);
}

static void mt_irq_op_type_set(mr_uint32_t irq, mr_uint8_t type, void *priv) {
    MR_IGNORE(irq);
    MR_IGNORE(type);
    MR_IGNORE(priv);
}

static void mt_irq_op_enable(mr_uint32_t irq, void *priv) {
    MR_IGNORE(irq);
    MR_IGNORE(priv);
}

static void mt_irq_op_unmask(mr_uint32_t irq, void *priv) {
    MR_IGNORE(irq);
    MR_IGNORE(priv);
}

static void mt_irq_op_disable(mr_uint32_t irq, void *priv) {
    MR_IGNORE(irq);
    MR_IGNORE(priv);
}

static void mt_irq_op_mask(mr_uint32_t irq, void *priv) {
    MR_IGNORE(irq);
    MR_IGNORE(priv);
}

/* Irq controller no-op operations */
static const mr_irq_ops_t mt_irq_ops = {
    .priority_set = mt_irq_op_priority_set,
    .type_set = mt_irq_op_type_set,
    .enable = mt_irq_op_enable,
    .unmask = mt_irq_op_unmask,
    .disable = mt_irq_op_disable,
    .mask = mt_irq_op_mask,
};

static mr_irq_return_t mt_irq_top_deferred(mr_uint32_t irq, void *owner) {
    mt_irq_ctx_t *ctx = mt_irq_active;

    MR_IGNORE(irq);
    MR_IGNORE(owner);
    mr_atomic_fetch_add(&ctx->main_count, 1);
    return MR_IRQ_DEFERRED;
}

static mr_irq_return_t mt_irq_top_none(mr_uint32_t irq, void *owner) {
    mt_irq_ctx_t *ctx = mt_irq_active;

    MR_IGNORE(irq);
    MR_IGNORE(owner);
    mr_atomic_fetch_add(&ctx->main_count, 1);
    return MR_IRQ_NONE;
}

static mr_irq_return_t mt_irq_defer_entry1(mr_uint32_t irq, void *owner) {
    mt_irq_ctx_t *ctx = mt_irq_active;

    MR_IGNORE(irq);
    MR_IGNORE(owner);
    mr_atomic_fetch_add(&ctx->fires, 1);
    return MR_IRQ_HANDLED;
}

/* Handler thread: hammers the line with software handles */
typedef struct mt_irq_arg {
    mt_irq_ctx_t *ctx;
    int count;
    mr_uint32_t line;
    int helpers;
} mt_irq_arg_t;

static void *mt_irq_handler(void *argument) {
    mt_irq_arg_t *arg = argument;
    int i;

    for (i = 0; i < arg->count; i++) {
        mr_irq_handle(arg->line);
        mt_sched_point(i);
    }
    mr_atomic_fetch_add(&arg->ctx->handles_done, 1);
    return MR_NULL;
}

static void *mt_irq_executor(void *argument) {
    mt_irq_arg_t *arg = argument;
    int idle = 0;
    int spins = 0;

    while (spins < MT_SCHED_SPIN_LIMIT) {
        mr_irq_defer_execute();
        mt_sched_point(spins);
        spins++;
        if ((int)mr_atomic_load(&arg->ctx->handles_done) >= arg->helpers) {
            idle++;
            if (idle > MT_SCHED_IDLE_LIMIT) {
                break;
            }
        }
    }
    return MR_NULL;
}

MR_TEST(irq, mt_defer_edge_coalesce_concurrent) {
    mt_irq_arg_t args[MT_SCHED_THREADS + 1];
    mt_sched_thread_t threads[MT_SCHED_THREADS + 1];
    mt_irq_ctx_t ctx;
    unsigned int fires_before;
    int i;
    int ret;

    mr_memset(&ctx, 0, sizeof(ctx));
    mt_irq_active = &ctx;
    ret = mr_irq_init(&ctx.irq, 12, 12, &mt_irq_ops, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_irq_defer_request(12, mt_irq_top_deferred, &mt_irq_owner1,
                               MR_IRQ_EDGE_RISING, mt_irq_defer_entry1);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    for (i = 0; i < MT_SCHED_THREADS; i++) {
        args[i].ctx = &ctx;
        args[i].count = MT_SCHED_ITERS;
        args[i].line = 12;
        args[i].helpers = MT_SCHED_THREADS;
        threads[i].started = 0;
        threads[i].id = i;
        ret = pthread_create(&threads[i].handle, MR_NULL, mt_irq_handler,
                             &args[i]);
        MR_TEST_EXPECT_EQ_INT(ret, 0);
        if (ret != 0) {
            break;
        }
        threads[i].started = 1;
    }
    args[MT_SCHED_THREADS].ctx = &ctx;
    args[MT_SCHED_THREADS].count = 0;
    args[MT_SCHED_THREADS].line = 12;
    args[MT_SCHED_THREADS].helpers = MT_SCHED_THREADS;
    threads[MT_SCHED_THREADS].started = 0;
    threads[MT_SCHED_THREADS].id = -1;
    ret = pthread_create(&threads[MT_SCHED_THREADS].handle, MR_NULL,
                         mt_irq_executor, &args[MT_SCHED_THREADS]);
    if (ret == 0) {
        threads[MT_SCHED_THREADS].started = 1;
    }
    MR_TEST_EXPECT_EQ_INT(mt_sched_join(threads, MT_SCHED_THREADS), 0);
    MR_TEST_EXPECT_EQ_INT(
        mt_sched_join(&threads[MT_SCHED_THREADS],
                      threads[MT_SCHED_THREADS].started ? 1 : 0),
        0);

    /* Flush anything still queued in the defer workqueue */
    for (i = 0; i < 200; i++) {
        mr_irq_defer_execute();
    }

    /* Every handle ran the top handler once, defer fires never exceed
     * the handle count */
    MR_TEST_EXPECT_EQ_UINT((mr_uint32_t)mr_atomic_load(&ctx.main_count),
                           (mr_uint32_t)(MT_SCHED_THREADS * MT_SCHED_ITERS));
    MR_TEST_EXPECT_GE_UINT((mr_uint32_t)mr_atomic_load(&ctx.fires), 1);
    MR_TEST_EXPECT_LE_UINT((mr_uint32_t)mr_atomic_load(&ctx.fires),
                           (mr_uint32_t)(MT_SCHED_THREADS * MT_SCHED_ITERS));

    /* The line must rearm after the queue drained */
    fires_before = (unsigned int)mr_atomic_load(&ctx.fires);
    ret = (int)mr_irq_handle(12);
    MR_TEST_EXPECT_EQ_INT(ret, MR_IRQ_DEFERRED);
    for (i = 0; i < 100; i++) {
        mr_irq_defer_execute();
    }
    MR_TEST_EXPECT_EQ_UINT((mr_uint32_t)mr_atomic_load(&ctx.fires),
                           fires_before + 1);

    MR_TEST_EXPECT_EQ_INT(mr_irq_free(12, &mt_irq_owner1), 0);
    mr_irq_defer_execute();
    MR_TEST_EXPECT_EQ_INT(mr_irq_del(&ctx.irq), 0);
    mt_irq_active = MR_NULL;
    /* Potential bug: in mr_irq_handle the window between action->pending--
     * and the deferred enqueue pending++ lets a concurrent mr_irq_free see
     * pending == 0 and free the action while this thread still enqueues
     * action->defer(use after free) */
}

static mr_irq_return_t mt_irq_top_deferred2(mr_uint32_t irq, void *owner) {
    mt_irq_ctx_t *ctx = mt_irq_active;

    MR_IGNORE(irq);
    MR_IGNORE(owner);
    mr_atomic_fetch_add(&ctx->main2_count, 1);
    return MR_IRQ_DEFERRED;
}

static mr_irq_return_t mt_irq_defer_entry_none(mr_uint32_t irq, void *owner) {
    mt_irq_ctx_t *ctx = mt_irq_active;

    MR_IGNORE(irq);
    MR_IGNORE(owner);
    mr_atomic_fetch_add(&ctx->fires2, 1);
    return MR_IRQ_HANDLED;
}

MR_TEST(irq, mt_shared_line_deferred_actions) {
    mt_irq_arg_t args[MT_SCHED_THREADS + 1];
    mt_sched_thread_t threads[MT_SCHED_THREADS + 1];
    mt_irq_ctx_t ctx;
    int i;
    int ret;

    mr_memset(&ctx, 0, sizeof(ctx));
    mt_irq_active = &ctx;
    ret = mr_irq_init(&ctx.irq, 13, 13, &mt_irq_ops, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_irq_defer_request(13, mt_irq_top_none, &mt_irq_owner1,
                               MR_IRQ_SHARED | MR_IRQ_LEVEL_HIGH,
                               mt_irq_defer_entry_none);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_irq_defer_request(13, mt_irq_top_deferred2, &mt_irq_owner2,
                               MR_IRQ_SHARED | MR_IRQ_LEVEL_HIGH,
                               mt_irq_defer_entry1);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    for (i = 0; i < MT_SCHED_THREADS; i++) {
        args[i].ctx = &ctx;
        args[i].count = MT_SCHED_ITERS;
        args[i].line = 13;
        args[i].helpers = MT_SCHED_THREADS;
        threads[i].started = 0;
        threads[i].id = i;
        ret = pthread_create(&threads[i].handle, MR_NULL, mt_irq_handler,
                             &args[i]);
        MR_TEST_EXPECT_EQ_INT(ret, 0);
        if (ret != 0) {
            break;
        }
        threads[i].started = 1;
    }
    args[MT_SCHED_THREADS].ctx = &ctx;
    args[MT_SCHED_THREADS].count = 0;
    args[MT_SCHED_THREADS].line = 13;
    args[MT_SCHED_THREADS].helpers = MT_SCHED_THREADS;
    threads[MT_SCHED_THREADS].started = 0;
    threads[MT_SCHED_THREADS].id = -1;
    ret = pthread_create(&threads[MT_SCHED_THREADS].handle, MR_NULL,
                         mt_irq_executor, &args[MT_SCHED_THREADS]);
    if (ret == 0) {
        threads[MT_SCHED_THREADS].started = 1;
    }
    MR_TEST_EXPECT_EQ_INT(mt_sched_join(threads, MT_SCHED_THREADS), 0);
    MR_TEST_EXPECT_EQ_INT(
        mt_sched_join(&threads[MT_SCHED_THREADS],
                      threads[MT_SCHED_THREADS].started ? 1 : 0),
        0);

    for (i = 0; i < 200; i++) {
        mr_irq_defer_execute();
    }

    /* The chain runs both top handlers once per handle, the first action
     * returns none so only the second action is ever deferred */
    MR_TEST_EXPECT_EQ_UINT((mr_uint32_t)mr_atomic_load(&ctx.main_count),
                           (mr_uint32_t)(MT_SCHED_THREADS * MT_SCHED_ITERS));
    MR_TEST_EXPECT_EQ_UINT((mr_uint32_t)mr_atomic_load(&ctx.main2_count),
                           (mr_uint32_t)(MT_SCHED_THREADS * MT_SCHED_ITERS));
    MR_TEST_EXPECT_EQ_UINT((mr_uint32_t)mr_atomic_load(&ctx.fires2), 0);
    MR_TEST_EXPECT_GE_UINT((mr_uint32_t)mr_atomic_load(&ctx.fires), 1);
    MR_TEST_EXPECT_LE_UINT((mr_uint32_t)mr_atomic_load(&ctx.fires),
                           (mr_uint32_t)(MT_SCHED_THREADS * MT_SCHED_ITERS));

    MR_TEST_EXPECT_EQ_INT(mr_irq_free(13, &mt_irq_owner1), 0);
    MR_TEST_EXPECT_EQ_INT(mr_irq_free(13, &mt_irq_owner2), 0);
    mr_irq_defer_execute();
    MR_TEST_EXPECT_EQ_INT(mr_irq_del(&ctx.irq), 0);
    mt_irq_active = MR_NULL;
}

#endif /* defined(MR_USE_IRQ_DEFER) */
#endif /* defined(MR_USE_IRQ) */

#endif /* defined(MR_USE_TEST_PTHREAD) */
#endif /* defined(MR_USE_TEST) */
