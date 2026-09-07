/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/test/mr_test.h>
#if defined(MR_USE_TEST)
#if defined(MR_USE_ATOMIC)
#include <mrX/libc/mr_atomic.h>

/* Test: static init */
MR_TEST(atomic, static_init) {
    mr_atomic_t v = MR_ATOMIC_INIT(12345);

    MR_TEST_ASSERT_EQ_INT(mr_atomic_load(&v), 12345);
}

/* Test: load/store                                                           */
MR_TEST(atomic, load_store) {
    mr_atomic_t v;

    mr_atomic_init(&v, 0);
    mr_atomic_store(&v, 42);
    MR_TEST_ASSERT_EQ_INT(mr_atomic_load(&v), 42);
}

/* Test: exchange                                                             */
MR_TEST(atomic, exchange) {
    mr_atomic_t v, old;

    mr_atomic_init(&v, 5);
    old = mr_atomic_exchange(&v, 7);
    MR_TEST_ASSERT_EQ_INT(old, 5);
    MR_TEST_ASSERT_EQ_INT(mr_atomic_load(&v), 7);
}

/* Test: compare-exchange success/fail                                        */
MR_TEST(atomic, compare_exchange) {
    mr_atomic_t v, expect;

    mr_atomic_init(&v, 3);
    mr_atomic_store(&expect, 3);
    MR_TEST_ASSERT_EQ_INT(mr_atomic_compare_exchange(&v, &expect, 9), MR_TRUE);
    MR_TEST_ASSERT_EQ_INT(v, 9);

    expect = 3;
    MR_TEST_ASSERT_EQ_INT(mr_atomic_compare_exchange(&v, &expect, 1), MR_FALSE);
    MR_TEST_ASSERT_EQ_INT(expect, 9);
}

/* Test: fetch-add                                                            */
MR_TEST(atomic, fetch_add) {
    mr_atomic_t v;

    mr_atomic_init(&v, 10);
    MR_TEST_ASSERT_EQ_INT(mr_atomic_fetch_add(&v, 5), 10);
    MR_TEST_ASSERT_EQ_INT(v, 15);
}

/* Test: fetch-sub                                                            */
MR_TEST(atomic, fetch_sub) {
    mr_atomic_t v;

    mr_atomic_init(&v, 20);
    MR_TEST_ASSERT_EQ_INT(mr_atomic_fetch_sub(&v, 8), 20);
    MR_TEST_ASSERT_EQ_INT(v, 12);
}

/* Test: bitwise fetch-and/or/xor                                             */
MR_TEST(atomic, bitwise) {
    mr_atomic_t v;

    mr_atomic_init(&v, 0x0F);
    MR_TEST_ASSERT_EQ_INT(mr_atomic_fetch_and(&v, 0x07), 0x0F);
    MR_TEST_ASSERT_EQ_INT(v, 0x07);

    MR_TEST_ASSERT_EQ_INT(mr_atomic_fetch_or(&v, 0x08), 0x07);
    MR_TEST_ASSERT_EQ_INT(v, 0x0F);

    MR_TEST_ASSERT_EQ_INT(mr_atomic_fetch_xor(&v, 0x0F), 0x0F);
    MR_TEST_ASSERT_EQ_INT(v, 0x00);
}

/* Test: zero and negative values                                             */
MR_TEST(atomic, boundary_values) {
    mr_atomic_t v;
    mr_atomic_t old;

    mr_atomic_init(&v, 0);
    MR_TEST_ASSERT_EQ_INT(mr_atomic_fetch_sub(&v, 1), 0);
    MR_TEST_ASSERT_EQ_INT(mr_atomic_load(&v), -1);

    old = mr_atomic_exchange(&v, 0x55AA);
    MR_TEST_ASSERT_EQ_INT(old, -1);
    MR_TEST_ASSERT_EQ_INT(mr_atomic_load(&v), 0x55AA);

    MR_TEST_ASSERT_EQ_INT(mr_atomic_fetch_add(&v, -0xAA), 0x55AA);
    MR_TEST_ASSERT_EQ_INT(mr_atomic_load(&v), 0x5500);
}

/* Test: compare-exchange updates the expected value on mismatch              */
MR_TEST(atomic, compare_exchange_mismatch) {
    mr_atomic_t v;
    mr_atomic_t expect;

    mr_atomic_init(&v, 0x1234);
    expect = 0x4321;
    MR_TEST_ASSERT_EQ_INT(mr_atomic_compare_exchange(&v, &expect, 0x9999),
                          MR_FALSE);
    MR_TEST_ASSERT_EQ_INT(expect, 0x1234);
    MR_TEST_ASSERT_EQ_INT(mr_atomic_load(&v), 0x1234);

    MR_TEST_ASSERT_EQ_INT(mr_atomic_compare_exchange(&v, &expect, 0x9999),
                          MR_TRUE);
    MR_TEST_ASSERT_EQ_INT(mr_atomic_load(&v), 0x9999);
}

#if defined(MR_USE_TEST_PTHREAD)
#include <pthread.h>

#define ATOMIC_MT_THREADS (4)
#define ATOMIC_MT_ITERS   (20000)

typedef struct atomic_mt_ctx {
    mr_atomic_t *value;
    int iters;
} atomic_mt_ctx_t;

static void *atomic_mt_worker(void *arg) {
    atomic_mt_ctx_t *ctx = arg;
    int i;

    for (i = 0; i < ctx->iters; i++) {
        mr_atomic_fetch_add(ctx->value, 1);
    }
    return MR_NULL;
}

MR_TEST(atomic, concurrent_fetch_add) {
    pthread_t threads[ATOMIC_MT_THREADS];
    atomic_mt_ctx_t ctx;
    mr_atomic_t value;
    int i;
    int ret;

    mr_atomic_init(&value, 0);
    ctx.value = &value;
    ctx.iters = ATOMIC_MT_ITERS;

    for (i = 0; i < ATOMIC_MT_THREADS; i++) {
        ret = pthread_create(&threads[i], MR_NULL, atomic_mt_worker, &ctx);
        MR_TEST_ASSERT_EQ_INT(ret, 0);
    }
    for (i = 0; i < ATOMIC_MT_THREADS; i++) {
        MR_TEST_EXPECT_EQ_INT(pthread_join(threads[i], MR_NULL), 0);
    }
    MR_TEST_ASSERT_EQ_INT(mr_atomic_load(&value),
                          ATOMIC_MT_THREADS * ATOMIC_MT_ITERS);
}

static void *atomic_mt_cas_worker(void *arg) {
    atomic_mt_ctx_t *ctx = arg;
    mr_atomic_t expect;
    int i;

    for (i = 0; i < ctx->iters; i++) {
        expect = mr_atomic_load(ctx->value);
        while (mr_atomic_compare_exchange(ctx->value, &expect, expect + 1)
               == MR_FALSE) {
        }
    }
    return MR_NULL;
}

MR_TEST(atomic, concurrent_compare_exchange) {
    pthread_t threads[ATOMIC_MT_THREADS];
    atomic_mt_ctx_t ctx;
    mr_atomic_t value;
    int i;
    int ret;

    mr_atomic_init(&value, 0);
    ctx.value = &value;
    ctx.iters = ATOMIC_MT_ITERS;

    for (i = 0; i < ATOMIC_MT_THREADS; i++) {
        ret = pthread_create(&threads[i], MR_NULL, atomic_mt_cas_worker, &ctx);
        MR_TEST_ASSERT_EQ_INT(ret, 0);
    }
    for (i = 0; i < ATOMIC_MT_THREADS; i++) {
        MR_TEST_EXPECT_EQ_INT(pthread_join(threads[i], MR_NULL), 0);
    }
    MR_TEST_ASSERT_EQ_INT(mr_atomic_load(&value),
                          ATOMIC_MT_THREADS * ATOMIC_MT_ITERS);
}

static void *atomic_mt_xor_worker(void *arg) {
    atomic_mt_ctx_t *ctx = arg;
    int i;

    for (i = 0; i < ctx->iters; i++) {
        mr_atomic_fetch_xor(ctx->value, 1);
    }
    return MR_NULL;
}

MR_TEST(atomic, concurrent_fetch_xor) {
    pthread_t threads[ATOMIC_MT_THREADS];
    atomic_mt_ctx_t ctx;
    mr_atomic_t value;
    int i;
    int ret;

    mr_atomic_init(&value, 0);
    ctx.value = &value;
    ctx.iters = ATOMIC_MT_ITERS;

    for (i = 0; i < ATOMIC_MT_THREADS; i++) {
        ret = pthread_create(&threads[i], MR_NULL, atomic_mt_xor_worker, &ctx);
        MR_TEST_ASSERT_EQ_INT(ret, 0);
    }
    for (i = 0; i < ATOMIC_MT_THREADS; i++) {
        MR_TEST_EXPECT_EQ_INT(pthread_join(threads[i], MR_NULL), 0);
    }
    MR_TEST_ASSERT_EQ_INT(mr_atomic_load(&value), 0);
}
#endif /* defined(MR_USE_TEST_PTHREAD) */

#endif /* defined(MR_USE_ATOMIC) */
#endif /* defined(MR_USE_TEST) */
