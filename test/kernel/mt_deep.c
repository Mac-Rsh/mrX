/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2026-08-30        MacRsh        Multi-thread deep test version
 */

#include <mrX/test/mr_test.h>
#if defined(MR_USE_TEST)
#include <mrX/kernel/mr_fifo.h>
#include <mrX/kernel/mr_ref.h>
#include <mrX/kernel/mr_spinlock.h>
#include <mrX/libc/mr_atomic.h>
#include <mrX/libc/mr_string.h>
#if defined(MR_USE_TEST_PTHREAD)
#include <pthread.h>

/* Multi-thread test thread count definition */
#define MT_THREADS (4)

/* Multi-thread test iteration count definition */
#define MT_ITERS (200)

/* Multi-thread test per-push retry limit definition */
#define MT_PUSH_RETRY (1000)

/* Multi-thread test spin safety limit definition */
#define MT_SPIN_LIMIT (2000000)

/* Fifo test tag encoding definition */
#define MT_FIFO_TAG(_pid, _seq)                                                \
    (((mr_uint32_t)(_pid) << 16) | (mr_uint32_t)(_seq))

/* Fifo test tag slot number definition */
#define MT_FIFO_SLOTS (MT_THREADS * MT_ITERS)

/* Multi-thread fifo context type */
typedef struct mt_fifo_ctx {
    mr_fifo_t fifo;
    mr_spinlock_t producer_lock;
    mr_spinlock_t consumer_lock;
    mr_atomic_t pushed;
    mr_atomic_t finished;
    mr_atomic_t popped;
    mr_atomic_t invalid;
    mr_atomic_t start;
    mr_atomic_t producers_done;
    mr_uint8_t seen[MT_FIFO_SLOTS];
    int producers;
    int consumers;
} mt_fifo_ctx_t;

/* Multi-thread thread argument type */
typedef struct mt_thread {
    pthread_t handle;
    int started;
    int id;
} mt_thread_t;

/* Multi-thread fifo worker argument type */
typedef struct mt_fifo_arg {
    mt_thread_t thread;
    mt_fifo_ctx_t *ctx;
    int count;
} mt_fifo_arg_t;

/* Multi-thread ref context type */
typedef struct mt_ref_ctx {
    mr_ref_t ref;
    mr_atomic_t releases;
} mt_ref_ctx_t;

/* Multi-thread ref worker argument type */
typedef struct mt_ref_arg {
    mt_thread_t thread;
    mt_ref_ctx_t *ctx;
    int count;
} mt_ref_arg_t;

/* Multi-thread spinlock context type */
typedef struct mt_spin_ctx {
    mr_spinlock_t lock_a;
    mr_spinlock_t lock_b;
    mr_uint32_t counter;
} mt_spin_ctx_t;

/* Multi-thread spinlock worker argument type */
typedef struct mt_spin_arg {
    mt_thread_t thread;
    mt_spin_ctx_t *ctx;
    int count;
} mt_spin_arg_t;

static void mt_fifo_record(mt_fifo_ctx_t *ctx, mr_uint32_t tag) {
    mr_uint32_t producer = tag >> 16;
    mr_uint32_t sequence = tag & 0xffffU;
    mr_uint32_t index;

    mr_atomic_fetch_add(&ctx->popped, 1);
    if ((producer >= MT_THREADS) || (sequence >= MT_ITERS)) {
        mr_atomic_fetch_add(&ctx->invalid, 1);
        return;
    }

    index = (producer * MT_ITERS) + sequence;
    if (ctx->seen[index] != 0) {
        ctx->seen[index] = 2;
    } else {
        ctx->seen[index] = 1;
    }
}

static int mt_fifo_dup_count(mt_fifo_ctx_t *ctx) {
    int dups = 0;
    mr_uint32_t i;

    for (i = 0; i < MT_FIFO_SLOTS; i++) {
        if (ctx->seen[i] == 2) {
            dups++;
        }
    }
    return dups;
}

static void *mt_fifo_producer(void *argument) {
    mt_fifo_arg_t *arg = argument;
    mt_fifo_ctx_t *ctx = arg->ctx;
    int pid = arg->thread.id;
    mr_uint32_t pushed;
    int i, retry;

    while (mr_atomic_load(&ctx->start) == 0) {
    }

    for (i = 0; i < arg->count; i++) {
        mr_uint32_t tag = MT_FIFO_TAG(pid, i);
        retry = 0;
        pushed = 0;
        while ((pushed == 0) && (retry < MT_PUSH_RETRY)) {
            mr_spinlock_lock(&ctx->producer_lock);
            pushed = mr_fifo_push(&ctx->fifo, &tag);
            mr_spinlock_unlock(&ctx->producer_lock);
            retry++;
        }
        if (pushed != 0) {
            mr_atomic_fetch_add(&ctx->pushed, 1);
        }
    }
    mr_atomic_fetch_add(&ctx->producers_done, 1);
    return MR_NULL;
}

static void *mt_fifo_consumer(void *argument) {
    mt_fifo_arg_t *arg = argument;
    mt_fifo_ctx_t *ctx = arg->ctx;
    mr_uint32_t tag;
    mr_bool_t done;
    mr_uint32_t popped;
    int spins = 0;

    while (mr_atomic_load(&ctx->start) == 0) {
    }

    while (spins < MT_SPIN_LIMIT) {
        mr_spinlock_lock(&ctx->consumer_lock);
        popped = mr_fifo_pop(&ctx->fifo, &tag);
        done = (mr_atomic_load(&ctx->producers_done) == ctx->producers)
            && mr_fifo_is_empty(&ctx->fifo);
        if (popped != 0) {
            mt_fifo_record(ctx, tag);
        }
        mr_spinlock_unlock(&ctx->consumer_lock);
        if (popped != 0) {
            spins = 0;
            continue;
        }
        if (done) {
            break;
        }
        spins++;
    }
    return MR_NULL;
}

MR_TEST(fifo, mt_mpsc_push) {
    mt_fifo_arg_t args[MT_THREADS];
    mt_fifo_arg_t carg[2];
    mt_fifo_ctx_t ctx;
    mr_uint8_t buf[16 * sizeof(mr_uint32_t)];
    int created = 0;
    int ccreated = 0;
    int i;

    mr_memset(&ctx, 0, sizeof(ctx));
    mr_spinlock_init(&ctx.producer_lock);
    mr_spinlock_init(&ctx.consumer_lock);
    ctx.producers = MT_THREADS;
    ctx.consumers = 1;
    MR_TEST_ASSERT_EQ_UINT(
        mr_fifo_init(&ctx.fifo, buf, sizeof(buf), sizeof(mr_uint32_t)), 16);

    for (i = 0; i < MT_THREADS; i++) {
        args[i].thread.started = 0;
        args[i].thread.id = i;
        args[i].ctx = &ctx;
        args[i].count = MT_ITERS;
        if (pthread_create(&args[i].thread.handle, MR_NULL, mt_fifo_producer,
                           &args[i])
            != 0) {
            break;
        }
        args[i].thread.started = 1;
        created++;
    }
    ctx.producers = created;
    for (i = 0; i < ctx.consumers; i++) {
        carg[i].thread.started = 0;
        carg[i].thread.id = -1;
        carg[i].ctx = &ctx;
        carg[i].count = 0;
        if (pthread_create(&carg[i].thread.handle, MR_NULL, mt_fifo_consumer,
                           &carg[i])
            == 0) {
            carg[i].thread.started = 1;
            ccreated++;
        }
    }
    mr_atomic_store(&ctx.start, 1);
    for (i = 0; i < created; i++) {
        MR_TEST_EXPECT_EQ_INT(pthread_join(args[i].thread.handle, MR_NULL), 0);
    }
    for (i = 0; i < ccreated; i++) {
        MR_TEST_EXPECT_EQ_INT(pthread_join(carg[i].thread.handle, MR_NULL), 0);
    }
    MR_TEST_EXPECT_EQ_INT(created, MT_THREADS);
    MR_TEST_EXPECT_EQ_INT(ccreated, ctx.consumers);

    /* Every successfully pushed element must be popped exactly once */
    MR_TEST_EXPECT_EQ_UINT(mr_atomic_load(&ctx.popped),
                           mr_atomic_load(&ctx.pushed));
    MR_TEST_EXPECT_EQ_UINT(mr_atomic_load(&ctx.invalid), 0);
    MR_TEST_EXPECT_EQ_INT(mt_fifo_dup_count(&ctx), 0);
}

MR_TEST(fifo, mt_spmc_pop) {
    mt_fifo_arg_t carg[MT_THREADS];
    mt_fifo_ctx_t ctx;
    mr_uint8_t buf[32 * sizeof(mr_uint32_t)];
    mr_uint32_t tag;
    int created = 0;
    int i;

    mr_memset(&ctx, 0, sizeof(ctx));
    mr_spinlock_init(&ctx.producer_lock);
    mr_spinlock_init(&ctx.consumer_lock);
    ctx.producers = 1;
    ctx.consumers = MT_THREADS;
    MR_TEST_ASSERT_EQ_UINT(
        mr_fifo_init(&ctx.fifo, buf, sizeof(buf), sizeof(mr_uint32_t)), 32);

    /* Start serialized consumers before filling the bounded fifo */
    for (i = 0; i < MT_THREADS; i++) {
        carg[i].thread.started = 0;
        carg[i].thread.id = -1;
        carg[i].ctx = &ctx;
        carg[i].count = 0;
        if (pthread_create(&carg[i].thread.handle, MR_NULL, mt_fifo_consumer,
                           &carg[i])
            == 0) {
            carg[i].thread.started = 1;
            created++;
        }
    }
    if (created == 0) {
        MR_TEST_EXPECT_EQ_INT(created, MT_THREADS);
        return;
    }
    mr_atomic_store(&ctx.start, 1);

    /* Single producer pushes a known set of tags */
    for (i = 0; i < MT_ITERS; i++) {
        tag = MT_FIFO_TAG(0, i);
        while (mr_fifo_push(&ctx.fifo, &tag) == 0) {
        }
        mr_atomic_fetch_add(&ctx.pushed, 1);
    }
    mr_atomic_store(&ctx.producers_done, 1);

    for (i = 0; i < created; i++) {
        MR_TEST_EXPECT_EQ_INT(pthread_join(carg[i].thread.handle, MR_NULL), 0);
    }
    MR_TEST_EXPECT_EQ_INT(created, MT_THREADS);

    /* Multiple consumers popping one fifo must drain it exactly once */
    MR_TEST_EXPECT_EQ_UINT(mr_atomic_load(&ctx.popped),
                           mr_atomic_load(&ctx.pushed));
    MR_TEST_EXPECT_EQ_UINT(mr_atomic_load(&ctx.invalid), 0);
    MR_TEST_EXPECT_EQ_INT(mt_fifo_dup_count(&ctx), 0);
}

MR_TEST(fifo, mt_mpmc_ring) {
    mt_fifo_arg_t args[2];
    mt_fifo_arg_t carg[2];
    mt_fifo_ctx_t ctx;
    mr_uint8_t buf[16 * sizeof(mr_uint32_t)];
    int created = 0;
    int ccreated = 0;
    int i;

    mr_memset(&ctx, 0, sizeof(ctx));
    mr_spinlock_init(&ctx.producer_lock);
    mr_spinlock_init(&ctx.consumer_lock);
    ctx.producers = 2;
    ctx.consumers = 2;
    MR_TEST_ASSERT_EQ_UINT(
        mr_fifo_init(&ctx.fifo, buf, sizeof(buf), sizeof(mr_uint32_t)), 16);

    for (i = 0; i < 2; i++) {
        args[i].thread.started = 0;
        args[i].thread.id = i;
        args[i].ctx = &ctx;
        args[i].count = MT_ITERS;
        if (pthread_create(&args[i].thread.handle, MR_NULL, mt_fifo_producer,
                           &args[i])
            != 0) {
            break;
        }
        args[i].thread.started = 1;
        created++;
    }
    ctx.producers = created;
    for (i = 0; i < 2; i++) {
        carg[i].thread.started = 0;
        carg[i].thread.id = -1;
        carg[i].ctx = &ctx;
        carg[i].count = 0;
        if (pthread_create(&carg[i].thread.handle, MR_NULL, mt_fifo_consumer,
                           &carg[i])
            == 0) {
            carg[i].thread.started = 1;
            ccreated++;
        }
    }
    mr_atomic_store(&ctx.start, 1);
    for (i = 0; i < created; i++) {
        MR_TEST_EXPECT_EQ_INT(pthread_join(args[i].thread.handle, MR_NULL), 0);
    }
    for (i = 0; i < ccreated; i++) {
        MR_TEST_EXPECT_EQ_INT(pthread_join(carg[i].thread.handle, MR_NULL), 0);
    }
    MR_TEST_EXPECT_EQ_INT(created, 2);
    MR_TEST_EXPECT_EQ_INT(ccreated, 2);

    /* Element conservation must hold even in the fully concurrent case */
    MR_TEST_EXPECT_EQ_UINT(mr_atomic_load(&ctx.popped),
                           mr_atomic_load(&ctx.pushed));
    MR_TEST_EXPECT_EQ_UINT(mr_atomic_load(&ctx.invalid), 0);
    MR_TEST_EXPECT_EQ_INT(mt_fifo_dup_count(&ctx), 0);
}

MR_TEST(fifo, claim_serialized_publish) {
    mt_fifo_ctx_t ctx;
    mr_uint32_t buf[16];
    mr_uint32_t *slot;
    mr_uint32_t tag;
    int producer, i;

    mr_memset(&ctx, 0, sizeof(ctx));
    MR_TEST_ASSERT_EQ_UINT(
        mr_fifo_init(&ctx.fifo, buf, sizeof(buf), sizeof(mr_uint32_t)), 16);

    for (i = 0; i < MT_ITERS; i++) {
        for (producer = 0; producer < 2; producer++) {
            tag = MT_FIFO_TAG(producer, i);
            MR_TEST_ASSERT_EQ_UINT(
                mr_fifo_write_claim(&ctx.fifo, 0, (void **)&slot, 1), 1);
            *slot = tag;
            MR_TEST_ASSERT_EQ_UINT(mr_fifo_write_finish(&ctx.fifo, 1), 1);
            mr_atomic_fetch_add(&ctx.finished, 1);

            MR_TEST_ASSERT_EQ_UINT(mr_fifo_pop(&ctx.fifo, &tag), 1);
            mt_fifo_record(&ctx, tag);
        }
    }

    MR_TEST_EXPECT_EQ_UINT(mr_atomic_load(&ctx.popped),
                           mr_atomic_load(&ctx.finished));
    MR_TEST_EXPECT_EQ_UINT(mr_atomic_load(&ctx.invalid), 0);
    MR_TEST_EXPECT_EQ_INT(mt_fifo_dup_count(&ctx), 0);
    MR_TEST_EXPECT_EQ_INT(mr_fifo_is_empty(&ctx.fifo), MR_TRUE);
}

static void mt_ref_release(mr_ref_t *ref) {
    mr_ref_t *target = ref;
    mt_ref_ctx_t *ctx;

    /* MR_CONTAINER_OF returns void*, assign to a typed pointer first */
    ctx = (mt_ref_ctx_t *)MR_CONTAINER_OF(target, mt_ref_ctx_t, ref);
    mr_atomic_fetch_add(&ctx->releases, 1);
}

static void *mt_ref_hammer_worker(void *argument) {
    mt_ref_arg_t *arg = argument;
    mt_ref_ctx_t *ctx = arg->ctx;
    int i;

    for (i = 0; i < arg->count; i++) {
        if (mr_ref_get(&ctx->ref)) {
            mr_ref_put(&ctx->ref, mt_ref_release);
        }
    }
    return MR_NULL;
}

static void *mt_ref_put_worker(void *argument) {
    mt_ref_arg_t *arg = argument;
    mt_ref_ctx_t *ctx = arg->ctx;

    mr_ref_put(&ctx->ref, mt_ref_release);
    return MR_NULL;
}

MR_TEST(ref, mt_get_put_hammer) {
    mt_ref_arg_t args[MT_THREADS];
    mt_ref_ctx_t ctx;
    int created = 0;
    int i;

    mr_ref_init(&ctx.ref);
    mr_atomic_init(&ctx.releases, 0);

    for (i = 0; i < MT_THREADS; i++) {
        args[i].thread.started = 0;
        args[i].thread.id = i;
        args[i].ctx = &ctx;
        args[i].count = MT_ITERS;
        if (pthread_create(&args[i].thread.handle, MR_NULL,
                           mt_ref_hammer_worker, &args[i])
            != 0) {
            break;
        }
        args[i].thread.started = 1;
        created++;
    }
    for (i = 0; i < created; i++) {
        MR_TEST_EXPECT_EQ_INT(pthread_join(args[i].thread.handle, MR_NULL), 0);
    }

    /* Every balanced get and put pair must leave the ref untouched */
    MR_TEST_EXPECT_EQ_UINT((mr_uint32_t)MR_REF_COUNT(ctx.ref), 1);
    MR_TEST_EXPECT_EQ_UINT((mr_uint32_t)MR_REF_IS_INITED(ctx.ref), MR_TRUE);
    MR_TEST_EXPECT_EQ_UINT((mr_uint32_t)mr_atomic_load(&ctx.releases), 0);
}

MR_TEST(ref, mt_release_race_to_zero) {
    mt_ref_arg_t args[2];
    mt_ref_ctx_t ctx;
    int created = 0;
    int i;

    mr_ref_init(&ctx.ref);
    mr_atomic_init(&ctx.releases, 0);

    /* Take one extra reference so the count is two before the race */
    MR_TEST_EXPECT_EQ_INT(mr_ref_get(&ctx.ref), MR_TRUE);

    for (i = 0; i < 2; i++) {
        args[i].thread.started = 0;
        args[i].thread.id = i;
        args[i].ctx = &ctx;
        args[i].count = 0;
        if (pthread_create(&args[i].thread.handle, MR_NULL, mt_ref_put_worker,
                           &args[i])
            != 0) {
            break;
        }
        args[i].thread.started = 1;
        created++;
    }
    for (i = 0; i < created; i++) {
        MR_TEST_EXPECT_EQ_INT(pthread_join(args[i].thread.handle, MR_NULL), 0);
    }

    /* Exactly one put must observe the last reference and release it */
    MR_TEST_EXPECT_EQ_UINT((mr_uint32_t)mr_atomic_load(&ctx.releases), 1);
    MR_TEST_EXPECT_EQ_UINT((mr_uint32_t)MR_REF_COUNT(ctx.ref), 0);
    MR_TEST_EXPECT_EQ_UINT((mr_uint32_t)MR_REF_IS_INITED(ctx.ref), MR_FALSE);
}

MR_TEST(ref, mt_put_below_zero) {
    mt_ref_arg_t args[MT_THREADS];
    mt_ref_ctx_t ctx;
    int created = 0;
    int i;

    mr_ref_init(&ctx.ref);
    mr_atomic_init(&ctx.releases, 0);

    /* The count is one, so all puts after the releasing put are API misuse */
    for (i = 0; i < MT_THREADS; i++) {
        args[i].thread.started = 0;
        args[i].thread.id = i;
        args[i].ctx = &ctx;
        args[i].count = 0;
        if (pthread_create(&args[i].thread.handle, MR_NULL, mt_ref_put_worker,
                           &args[i])
            != 0) {
            break;
        }
        args[i].thread.started = 1;
        created++;
    }
    for (i = 0; i < created; i++) {
        MR_TEST_EXPECT_EQ_INT(pthread_join(args[i].thread.handle, MR_NULL), 0);
    }

    /* Exactly one release fires and the underflowed ref can not be revived */
    MR_TEST_EXPECT_EQ_UINT((mr_uint32_t)mr_atomic_load(&ctx.releases), 1);
    MR_TEST_EXPECT_EQ_INT(MR_REF_IS_INITED(ctx.ref), MR_FALSE);
    MR_TEST_EXPECT_EQ_INT(mr_ref_get(&ctx.ref), MR_FALSE);
    /* Safe drain: the release callback owns no memory, reinit is harmless */
    mr_ref_init(&ctx.ref);
}

static void *mt_spin_hammer_worker(void *argument) {
    mt_spin_arg_t *arg = argument;
    mt_spin_ctx_t *ctx = arg->ctx;
    int i;

    for (i = 0; i < arg->count; i++) {
        mr_spinlock_lock(&ctx->lock_a);
        ctx->counter++;
        mr_spinlock_unlock(&ctx->lock_a);
    }
    return MR_NULL;
}

static void *mt_spin_nest_worker(void *argument) {
    mt_spin_arg_t *arg = argument;
    mt_spin_ctx_t *ctx = arg->ctx;
    int mask;
    int i;

    for (i = 0; i < arg->count; i++) {
        mask = mr_spinlock_lock_irqsave(&ctx->lock_a);
        mr_spinlock_lock(&ctx->lock_b);
        ctx->counter++;
        mr_spinlock_unlock(&ctx->lock_b);
        mr_spinlock_unlock_irqrestore(&ctx->lock_a, mask);
    }
    return MR_NULL;
}

static void *mt_spin_ordered_worker(void *argument) {
    mt_spin_arg_t *arg = argument;
    mt_spin_ctx_t *ctx = arg->ctx;
    int i;

    for (i = 0; i < arg->count; i++) {
        /* Every thread uses the same a then b order, no inversion exists */
        mr_spinlock_lock(&ctx->lock_a);
        mr_spinlock_lock(&ctx->lock_b);
        ctx->counter++;
        mr_spinlock_unlock(&ctx->lock_b);
        mr_spinlock_unlock(&ctx->lock_a);
    }
    return MR_NULL;
}

MR_TEST(spinlock, mt_contention_hammer) {
    mt_spin_arg_t args[MT_THREADS];
    mt_spin_ctx_t ctx;
    int created = 0;
    int i;

    mr_memset(&ctx, 0, sizeof(ctx));
    mr_spinlock_init(&ctx.lock_a);
    mr_spinlock_init(&ctx.lock_b);

    for (i = 0; i < MT_THREADS; i++) {
        args[i].thread.started = 0;
        args[i].thread.id = i;
        args[i].ctx = &ctx;
        args[i].count = 1000;
        if (pthread_create(&args[i].thread.handle, MR_NULL,
                           mt_spin_hammer_worker, &args[i])
            != 0) {
            break;
        }
        args[i].thread.started = 1;
        created++;
    }
    for (i = 0; i < created; i++) {
        MR_TEST_EXPECT_EQ_INT(pthread_join(args[i].thread.handle, MR_NULL), 0);
    }

    /* No increment may be lost under contention */
    MR_TEST_EXPECT_EQ_UINT(ctx.counter, (mr_uint32_t)(created * 1000));
    MR_TEST_EXPECT_EQ_UINT((mr_uint32_t)mr_spinlock_is_locked(&ctx.lock_a),
                           MR_FALSE);
}

MR_TEST(spinlock, mt_irqsave_nesting) {
    mt_spin_arg_t args[MT_THREADS];
    mt_spin_ctx_t ctx;
    int created = 0;
    int i;

    mr_memset(&ctx, 0, sizeof(ctx));
    mr_spinlock_init(&ctx.lock_a);
    mr_spinlock_init(&ctx.lock_b);

    for (i = 0; i < MT_THREADS; i++) {
        args[i].thread.started = 0;
        args[i].thread.id = i;
        args[i].ctx = &ctx;
        args[i].count = 500;
        if (pthread_create(&args[i].thread.handle, MR_NULL, mt_spin_nest_worker,
                           &args[i])
            != 0) {
            break;
        }
        args[i].thread.started = 1;
        created++;
    }
    for (i = 0; i < created; i++) {
        MR_TEST_EXPECT_EQ_INT(pthread_join(args[i].thread.handle, MR_NULL), 0);
    }

    /* The nested critical section must behave like a plain lock */
    MR_TEST_EXPECT_EQ_UINT(ctx.counter, (mr_uint32_t)(created * 500));
    MR_TEST_EXPECT_EQ_UINT((mr_uint32_t)mr_spinlock_is_locked(&ctx.lock_a),
                           MR_FALSE);
    MR_TEST_EXPECT_EQ_UINT((mr_uint32_t)mr_spinlock_is_locked(&ctx.lock_b),
                           MR_FALSE);
}

MR_TEST(spinlock, mt_two_lock_fixed_order) {
    mt_spin_arg_t args[MT_THREADS];
    mt_spin_ctx_t ctx;
    int created = 0;
    int i;

    mr_memset(&ctx, 0, sizeof(ctx));
    mr_spinlock_init(&ctx.lock_a);
    mr_spinlock_init(&ctx.lock_b);

    for (i = 0; i < MT_THREADS; i++) {
        args[i].thread.started = 0;
        args[i].thread.id = i;
        args[i].ctx = &ctx;
        args[i].count = 500;
        if (pthread_create(&args[i].thread.handle, MR_NULL,
                           mt_spin_ordered_worker, &args[i])
            != 0) {
            break;
        }
        args[i].thread.started = 1;
        created++;
    }
    for (i = 0; i < created; i++) {
        MR_TEST_EXPECT_EQ_INT(pthread_join(args[i].thread.handle, MR_NULL), 0);
    }

    /* Completion of all joins already proves no deadlock occurred */
    MR_TEST_EXPECT_EQ_UINT(ctx.counter, (mr_uint32_t)(created * 500));
    MR_TEST_EXPECT_EQ_UINT((mr_uint32_t)mr_spinlock_is_locked(&ctx.lock_a),
                           MR_FALSE);
    MR_TEST_EXPECT_EQ_UINT((mr_uint32_t)mr_spinlock_is_locked(&ctx.lock_b),
                           MR_FALSE);
}

#endif /* defined(MR_USE_TEST_PTHREAD) */
#endif /* defined(MR_USE_TEST) */
