/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/test/mr_test.h>
#if defined(MR_USE_TEST)
#include <mrX/kernel/mr_spinlock.h>

/* Tests */
MR_TEST(spinlock, static_init) {
    static mr_spinlock_t lock = MR_SPINLOCK_INIT();

    MR_TEST_ASSERT_EQ_INT(mr_spinlock_is_locked(&lock), 0);
}

MR_TEST(spinlock, lock_unlock) {
    mr_spinlock_t lock;

    mr_spinlock_init(&lock);
    MR_TEST_ASSERT_EQ_INT(mr_spinlock_is_locked(&lock), 0);

    mr_spinlock_lock(&lock);
    MR_TEST_EXPECT_EQ_INT(mr_spinlock_is_locked(&lock), 1);

    mr_spinlock_unlock(&lock);
    MR_TEST_ASSERT_EQ_INT(mr_spinlock_is_locked(&lock), 0);
}

MR_TEST(spinlock, try_lock) {
    mr_spinlock_t lock;

    mr_spinlock_init(&lock);

    /* Try lock on a free lock must succeed */
    MR_TEST_ASSERT_EQ_INT(mr_spinlock_try_lock(&lock), MR_TRUE);
    MR_TEST_ASSERT_EQ_INT(mr_spinlock_is_locked(&lock), 1);

    /* Try lock on a held lock must fail without spinning */
    MR_TEST_ASSERT_EQ_INT(mr_spinlock_try_lock(&lock), MR_FALSE);
    MR_TEST_ASSERT_EQ_INT(mr_spinlock_is_locked(&lock), 1);

    /* The lock must be usable again after unlock */
    mr_spinlock_unlock(&lock);
    MR_TEST_ASSERT_EQ_INT(mr_spinlock_try_lock(&lock), MR_TRUE);
    MR_TEST_ASSERT_EQ_INT(mr_spinlock_is_locked(&lock), 1);
    mr_spinlock_unlock(&lock);
    MR_TEST_ASSERT_EQ_INT(mr_spinlock_is_locked(&lock), 0);
}

MR_TEST(spinlock, irq_save_restore) {
    mr_spinlock_t lock;
    int mask;

    mr_spinlock_init(&lock);
    mask = mr_spinlock_lock_irqsave(&lock);
    MR_TEST_EXPECT_EQ_INT(mr_spinlock_is_locked(&lock), 1);

    mr_spinlock_unlock_irqrestore(&lock, mask);
    MR_TEST_ASSERT_EQ_INT(mr_spinlock_is_locked(&lock), 0);
}

MR_TEST(spinlock, repeated_lock_unlock) {
    mr_spinlock_t lock;
    int i;

    mr_spinlock_init(&lock);
    for (i = 0; i < 32; i++) {
        MR_TEST_ASSERT_EQ_INT(mr_spinlock_is_locked(&lock), 0);
        mr_spinlock_lock(&lock);
        MR_TEST_ASSERT_EQ_INT(mr_spinlock_is_locked(&lock), 1);
        mr_spinlock_unlock(&lock);
    }
    MR_TEST_ASSERT_EQ_INT(mr_spinlock_is_locked(&lock), 0);
}

#if defined(MR_USE_TEST_PTHREAD)
#include <pthread.h>

#define SPINLOCK_MT_THREADS (4)
#define SPINLOCK_MT_ITERS   (20000)

typedef struct spinlock_mt_ctx {
    mr_spinlock_t *lock;
    int *counter;
    int iters;
} spinlock_mt_ctx_t;

static void *spinlock_mt_worker(void *arg) {
    spinlock_mt_ctx_t *ctx = arg;
    int i;

    for (i = 0; i < ctx->iters; i++) {
        mr_spinlock_lock(ctx->lock);
        (*ctx->counter)++;
        mr_spinlock_unlock(ctx->lock);
    }
    return MR_NULL;
}

MR_TEST(spinlock, concurrent_inc) {
    pthread_t threads[SPINLOCK_MT_THREADS];
    spinlock_mt_ctx_t ctx;
    mr_spinlock_t lock;
    int counter = 0;
    int created;
    int i;
    int ret;

    mr_spinlock_init(&lock);
    ctx.lock = &lock;
    ctx.counter = &counter;
    ctx.iters = SPINLOCK_MT_ITERS;

    for (created = i = 0; i < SPINLOCK_MT_THREADS; i++) {
        ret = pthread_create(&threads[i], MR_NULL, spinlock_mt_worker, &ctx);
        MR_TEST_EXPECT_EQ_INT(ret, 0);
        if (ret != 0) {
            break;
        }
        created++;
    }
    for (i = 0; i < created; i++) {
        MR_TEST_EXPECT_EQ_INT(pthread_join(threads[i], MR_NULL), 0);
    }
    if (created != SPINLOCK_MT_THREADS) {
        return;
    }
    MR_TEST_ASSERT_EQ_INT(counter, SPINLOCK_MT_THREADS * SPINLOCK_MT_ITERS);
}
#endif /* defined(MR_USE_TEST_PTHREAD) */

#endif /* defined(MR_USE_TEST) */
