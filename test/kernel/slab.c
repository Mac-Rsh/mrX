/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/test/mr_test.h>
#if defined(MR_USE_TEST)
#include <mrX/kernel/mr_slab.h>
#include <mrX/libc/mr_string.h>

/* Constants */

#define TEST_SLAB_OBJ_SIZE 64
#define TEST_SLAB_OBJ_NUM  16
#define TEST_SLAB_MAP_SIZE MR_SLAB_MAP_SIZE(TEST_SLAB_OBJ_NUM)

/* Helpers */

static mr_uint8_t test_slab_map[TEST_SLAB_MAP_SIZE];
static mr_uint8_t test_slab_buf[TEST_SLAB_OBJ_SIZE * TEST_SLAB_OBJ_NUM];
static mr_slab_t test_slab = MR_SLAB_INIT(
    test_slab_map, test_slab_buf, TEST_SLAB_OBJ_SIZE, TEST_SLAB_OBJ_NUM);

static void slab_reset(void) {
    mr_memset(test_slab_map, 0, sizeof(test_slab_map));
}

static void canary_fill(void *p, mr_size_t usable) {
    mr_uint8_t *data;
    mr_size_t i;

    for (data = (mr_uint8_t *)p, i = 0; i < usable; ++i) {
        data[i] = (mr_uint8_t)(i & 0xFFU);
    }
}

static int canary_verify(void *p, mr_size_t usable) {
    mr_uint8_t *data;
    mr_size_t i;

    for (data = (mr_uint8_t *)p, i = 0; i < usable; ++i) {
        if (data[i] != (mr_uint8_t)(i & 0xFFU)) {
            return 0;
        }
    }
    return 1;
}

static void test_slab_free_unique(void **mem, mr_size_t count) {
    mr_size_t i;
    mr_size_t j;

    for (i = 0; i < count; i++) {
        if (mem[i] == MR_NULL) {
            continue;
        }
        for (j = 0; j < i; j++) {
            if (mem[i] == mem[j]) {
                break;
            }
        }
        if (j == i) {
            mr_slab_free(&test_slab, mem[i]);
        }
    }
}

MR_TEST(slab, basic) {
    void *p = MR_NULL;

    slab_reset();

    p = mr_slab_alloc(&test_slab);
    if (p == MR_NULL) {
        MR_TEST_EXPECT_NE_PTR(p, MR_NULL);
        goto _exit;
    }

    canary_fill(p, TEST_SLAB_OBJ_SIZE);
    MR_TEST_EXPECT_EQ_INT(canary_verify(p, TEST_SLAB_OBJ_SIZE), 1);

_exit:
    if (p != MR_NULL) {
        mr_slab_free(&test_slab, p);
    }
}

MR_TEST(slab, multi_alloc) {
    void *arr[TEST_SLAB_OBJ_NUM] = {MR_NULL};
    int i;

    slab_reset();

    for (i = 0; i < TEST_SLAB_OBJ_NUM; i++) {
        arr[i] = mr_slab_alloc(&test_slab);
        if (arr[i] == MR_NULL) {
            MR_TEST_EXPECT_NE_PTR(arr[i], MR_NULL);
            goto _exit;
        }
        canary_fill(arr[i], TEST_SLAB_OBJ_SIZE);
    }
    MR_TEST_EXPECT_EQ_PTR(mr_slab_alloc(&test_slab), MR_NULL);
    for (i = 0; i < TEST_SLAB_OBJ_NUM; i++) {
        MR_TEST_EXPECT_EQ_INT(canary_verify(arr[i], TEST_SLAB_OBJ_SIZE), 1);
        mr_slab_free(&test_slab, arr[i]);
        arr[i] = MR_NULL;
    }

    for (i = 0; i < TEST_SLAB_OBJ_NUM; i++) {
        arr[i] = mr_slab_alloc(&test_slab);
        if (arr[i] == MR_NULL) {
            MR_TEST_EXPECT_NE_PTR(arr[i], MR_NULL);
            goto _exit;
        }
    }

_exit:
    for (i = 0; i < TEST_SLAB_OBJ_NUM; i++) {
        if (arr[i] != MR_NULL) {
            mr_slab_free(&test_slab, arr[i]);
        }
    }
}

MR_TEST(slab, interleave) {
    void *arr[TEST_SLAB_OBJ_NUM] = {MR_NULL};
    int i;

    slab_reset();

    for (i = 0; i < TEST_SLAB_OBJ_NUM; i++) {
        arr[i] = mr_slab_alloc(&test_slab);
        if (arr[i] == MR_NULL) {
            MR_TEST_EXPECT_NE_PTR(arr[i], MR_NULL);
            goto _exit;
        }
        canary_fill(arr[i], TEST_SLAB_OBJ_SIZE);
    }
    for (i = 0; i < TEST_SLAB_OBJ_NUM; i += 2) {
        mr_slab_free(&test_slab, arr[i]);
        arr[i] = MR_NULL;
    }

    for (i = 0; i < TEST_SLAB_OBJ_NUM; i += 2) {
        arr[i] = mr_slab_alloc(&test_slab);
        if (arr[i] == MR_NULL) {
            MR_TEST_EXPECT_NE_PTR(arr[i], MR_NULL);
            goto _exit;
        }
        canary_fill(arr[i], TEST_SLAB_OBJ_SIZE);
    }

_exit:
    for (i = 0; i < TEST_SLAB_OBJ_NUM; i++) {
        if (arr[i] != MR_NULL) {
            mr_slab_free(&test_slab, arr[i]);
        }
    }
}

MR_TEST(slab, invalid_free) {
    mr_uint8_t invalid_buf[TEST_SLAB_OBJ_SIZE];
    void *p = MR_NULL;

    slab_reset();
    p = mr_slab_alloc(&test_slab);

    if (p == MR_NULL) {
        MR_TEST_EXPECT_NE_PTR(p, MR_NULL);
        goto _exit;
    }
    mr_slab_free(&test_slab, MR_NULL);
    mr_slab_free(&test_slab, invalid_buf);
    mr_slab_free(&test_slab, (void *)0xDEADBEEF);

_exit:
    if (p != MR_NULL) {
        mr_slab_free(&test_slab, p);
    }
}

MR_TEST(slab, init_map_size_limit) {
    mr_atomic_t map[1] = {0};
    mr_uint8_t buf[64 * 64];
    mr_slab_t slab;
    mr_uint32_t num;

    num = mr_slab_init(&slab, map, sizeof(map), buf, 64, 64);
    MR_TEST_ASSERT_EQ_UINT(num, MR_SLAB_MAP_BITS);
    MR_TEST_ASSERT_EQ_UINT(slab.num, MR_SLAB_MAP_BITS);
}

MR_TEST(slab, unaligned_free) {
    void *allocated[4] = {MR_NULL};
    int p2_index;

    slab_reset();

    allocated[0] = mr_slab_alloc(&test_slab);
    allocated[1] = mr_slab_alloc(&test_slab);
    if ((allocated[0] == MR_NULL) || (allocated[1] == MR_NULL)) {
        MR_TEST_EXPECT_NE_PTR(allocated[0], MR_NULL);
        MR_TEST_EXPECT_NE_PTR(allocated[1], MR_NULL);
        goto _exit;
    }

    mr_slab_free(&test_slab, (mr_uint8_t *)allocated[0] + 1);
    allocated[2] = mr_slab_alloc(&test_slab);
    p2_index = mr_slab_ptr2index(&test_slab, allocated[2]);
    MR_TEST_EXPECT_EQ_INT(p2_index, 2);
    if (p2_index != 2) {
        goto _exit;
    }

    mr_slab_free(&test_slab, allocated[0]);
    allocated[0] = MR_NULL;
    allocated[3] = mr_slab_alloc(&test_slab);
    MR_TEST_EXPECT_EQ_INT(mr_slab_ptr2index(&test_slab, allocated[3]), 0);

_exit:
    test_slab_free_unique(allocated, MR_ARRAY_SIZE(allocated));
}

MR_TEST(slab, index_conversion_boundaries) {
    void *first;
    void *last;

    slab_reset();
    first = mr_slab_index2ptr(&test_slab, 0);
    last = mr_slab_index2ptr(&test_slab, TEST_SLAB_OBJ_NUM - 1);

    MR_TEST_ASSERT_EQ_PTR(first, test_slab.buf);
    MR_TEST_ASSERT_EQ_INT(mr_slab_ptr2index(&test_slab, first), 0);
    MR_TEST_ASSERT_EQ_PTR(
        last, test_slab.buf + ((TEST_SLAB_OBJ_NUM - 1) * TEST_SLAB_OBJ_SIZE));
    MR_TEST_ASSERT_EQ_INT(mr_slab_ptr2index(&test_slab, last),
                          TEST_SLAB_OBJ_NUM - 1);
    MR_TEST_ASSERT_EQ_PTR(mr_slab_index2ptr(&test_slab, -1), MR_NULL);
    MR_TEST_ASSERT_EQ_PTR(mr_slab_index2ptr(&test_slab, TEST_SLAB_OBJ_NUM),
                          MR_NULL);
    MR_TEST_ASSERT_EQ_INT(mr_slab_ptr2index(&test_slab, MR_NULL), -1);
}

MR_TEST(slab, repeated_free) {
    void *p;
    void *next;

    slab_reset();
    p = mr_slab_alloc(&test_slab);
    MR_TEST_ASSERT_NE_PTR(p, MR_NULL);
    if (p == MR_NULL) {
        return;
    }

    mr_slab_free(&test_slab, p);
    mr_slab_free(&test_slab, p);
    next = mr_slab_alloc(&test_slab);
    MR_TEST_ASSERT_EQ_PTR(next, p);
    if (next != MR_NULL) {
        mr_slab_free(&test_slab, next);
    }
}

MR_TEST(slab, unaligned_map) {
    mr_uint8_t raw[sizeof(mr_atomic_t) * 2U + 1U];
    mr_uint8_t buf[64];
    mr_slab_t slab;
    mr_uint32_t num;

    num = mr_slab_init(&slab, raw + 1, sizeof(mr_atomic_t) * 2U, buf, 8, 8);
    MR_TEST_ASSERT_EQ_UINT(num, 0);
}

MR_TEST(slab, alloc_free_cycle) {
    void *first;
    void *again;

    slab_reset();
    first = mr_slab_alloc(&test_slab);
    MR_TEST_ASSERT_NE_PTR(first, MR_NULL);
    if (first == MR_NULL) {
        return;
    }
    mr_slab_free(&test_slab, first);
    again = mr_slab_alloc(&test_slab);
    MR_TEST_ASSERT_EQ_PTR(again, first);
    if (again != MR_NULL) {
        mr_slab_free(&test_slab, again);
    }
}

#if defined(MR_USE_TEST_PTHREAD)
#include <pthread.h>

#define SLAB_MT_THREADS (4)
#define SLAB_MT_ITERS   (2000)

typedef struct slab_mt_ctx {
    mr_slab_t *slab;
    int iters;
} slab_mt_ctx_t;

static void *slab_mt_worker(void *arg) {
    slab_mt_ctx_t *ctx = arg;
    void *p;
    int i;

    for (i = 0; i < ctx->iters; i++) {
        p = mr_slab_alloc(ctx->slab);
        if (p != MR_NULL) {
            mr_memset(p, (int)i, TEST_SLAB_OBJ_SIZE);
            mr_slab_free(ctx->slab, p);
        }
    }
    return MR_NULL;
}

MR_TEST(slab, concurrent_alloc_free) {
    pthread_t threads[SLAB_MT_THREADS];
    slab_mt_ctx_t ctx;
    void *held[TEST_SLAB_OBJ_NUM];
    int i;
    int ret;

    slab_reset();
    ctx.slab = &test_slab;
    ctx.iters = SLAB_MT_ITERS;

    for (i = 0; i < SLAB_MT_THREADS; i++) {
        ret = pthread_create(&threads[i], MR_NULL, slab_mt_worker, &ctx);
        MR_TEST_ASSERT_EQ_INT(ret, 0);
    }
    for (i = 0; i < SLAB_MT_THREADS; i++) {
        MR_TEST_EXPECT_EQ_INT(pthread_join(threads[i], MR_NULL), 0);
    }

    for (i = 0; i < TEST_SLAB_OBJ_NUM; i++) {
        held[i] = mr_slab_alloc(&test_slab);
        MR_TEST_ASSERT_NE_PTR(held[i], MR_NULL);
    }
    MR_TEST_ASSERT_EQ_PTR(mr_slab_alloc(&test_slab), MR_NULL);
    for (i = 0; i < TEST_SLAB_OBJ_NUM; i++) {
        if (held[i] != MR_NULL) {
            mr_slab_free(&test_slab, held[i]);
        }
    }
}
#endif /* defined(MR_USE_TEST_PTHREAD) */

#endif /* defined(MR_USE_TEST) */
