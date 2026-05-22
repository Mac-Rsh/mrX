/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/test/mr_test.h>
#if defined(MR_USE_TEST)
#include <mrX/libc/mr_malloc.h>

/* -------------------------------------------------------------------------- */
/* Utility: fill/verify canary pattern                                        */
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

/* -------------------------------------------------------------------------- */
/* Test: allocate, write pattern, verify, free                                */
static void test_malloc_free_basic(void) {
    mr_size_t alloc_usable;
    mr_size_t usable;
    void *p = MR_NULL;

#if defined(MR_USE_MALLOC)
    p = mr_malloc(0);
    MR_TEST_EXPECT_EQ_PTR(p, MR_NULL);
    if (p != MR_NULL) {
        mr_free(p);
        p = MR_NULL;
    }
    mr_free(MR_NULL);
#endif /* defined(MR_USE_MALLOC) */

    p = mr_malloc(128);
    if (p == MR_NULL) {
        MR_TEST_EXPECT_NE_PTR(p, MR_NULL);
        goto _exit;
    }

#if defined(MR_USE_MALLOC)
    usable = mr_malloc_usable_size(p);
    MR_TEST_EXPECT_GE_UINT(usable, 128);
    if (usable < 128) {
        goto _exit;
    }
    alloc_usable = usable;

    usable = mr_malloc_usable_size(MR_NULL);
    MR_TEST_EXPECT_EQ_UINT(usable, 0);
    if (usable != 0) {
        goto _exit;
    }

    usable = mr_malloc_usable_size((mr_uint8_t *)p + 32);
    MR_TEST_EXPECT_EQ_UINT(usable, 0);
    if (usable != 0) {
        goto _exit;
    }
#else
    alloc_usable = 128;
#endif /* defined(MR_USE_MALLOC) */

    canary_fill(p, alloc_usable);
    MR_TEST_EXPECT_EQ_INT(canary_verify(p, alloc_usable), 1);

_exit:
    mr_free(p);
}
MR_TEST_EXPORT(malloc, free_basic, test_malloc_free_basic);

/* -------------------------------------------------------------------------- */
/* Test: memory must be zero-filled                                           */
static void test_calloc_zero(void) {
    const mr_size_t n = 16, sz = 32;
    mr_size_t usable, i;
    void *p = MR_NULL;

#if defined(MR_USE_MALLOC)
    p = mr_calloc(0, 0);
    MR_TEST_EXPECT_EQ_PTR(p, MR_NULL);
    if (p != MR_NULL) {
        mr_free(p);
        p = MR_NULL;
    }
#endif /* defined(MR_USE_MALLOC) */

    p = mr_calloc(MR_SIZE_MAX >> 1, 1);
    MR_TEST_EXPECT_EQ_PTR(p, MR_NULL);
    if (p != MR_NULL) {
        mr_free(p);
        p = MR_NULL;
    }

    p = mr_calloc(n, sz);
    if (p == MR_NULL) {
        MR_TEST_EXPECT_NE_PTR(p, MR_NULL);
        goto _exit;
    }

#if defined(MR_USE_MALLOC)
    usable = mr_malloc_usable_size(p);
    MR_TEST_EXPECT_GE_UINT(usable, n * sz);
    if (usable < (n * sz)) {
        goto _exit;
    }
#else
    usable = n * sz;
#endif /* defined(MR_USE_MALLOC) */

    /* Verify all bytes are zero */
    for (i = 0; i < usable; ++i) {
        MR_TEST_EXPECT_EQ_INT(((mr_uint8_t *)p)[i], 0);
    }

_exit:
    mr_free(p);
}
MR_TEST_EXPORT(malloc, calloc_zero, test_calloc_zero);

/* -------------------------------------------------------------------------- */
/* Test: resize buffer and retain original data                               */
static void test_realloc_resize(void) {
    void *p1 = MR_NULL, *p2 = MR_NULL, *p3 = MR_NULL;

    p1 = mr_malloc(64);
    if (p1 == MR_NULL) {
        MR_TEST_EXPECT_NE_PTR(p1, MR_NULL);
        goto _exit;
    }
    canary_fill(p1, 64);

    p2 = mr_realloc(p1, MR_SIZE_MAX >> 1);
    MR_TEST_EXPECT_EQ_PTR(p2, MR_NULL);
    if (p2 != MR_NULL) {
        p1 = MR_NULL;
        goto _exit;
    }

    p2 = mr_realloc(p1, 256);
    if (p2 == MR_NULL) {
        MR_TEST_EXPECT_NE_PTR(p2, MR_NULL);
        goto _exit;
    }
    p1 = MR_NULL;
    MR_TEST_EXPECT_EQ_INT(canary_verify(p2, 64), 1);
    if (!canary_verify(p2, 64)) {
        goto _exit;
    }

    p3 = mr_realloc(p2, 32);
    if (p3 == MR_NULL) {
        MR_TEST_EXPECT_NE_PTR(p3, MR_NULL);
        goto _exit;
    }
    p2 = MR_NULL;
    MR_TEST_EXPECT_EQ_INT(canary_verify(p3, 32), 1);

_exit:
    mr_free(p1);
    mr_free(p2);
    mr_free(p3);
}
MR_TEST_EXPORT(malloc, realloc_resize, test_realloc_resize);

#if defined(MR_USE_MALLOC)
/* -------------------------------------------------------------------------- */
/* Test: Zero-size malloc should return NULL                                  */
static void test_malloc_zero(void) {
    void *p;

    p = mr_malloc(0);
    MR_TEST_EXPECT_EQ_PTR(p, MR_NULL);
    mr_free(p);
}
MR_TEST_EXPORT(malloc, malloc_zero, test_malloc_zero);
#endif /* defined(MR_USE_MALLOC) */

/* -------------------------------------------------------------------------- */
/* Test: allocate, free, re-allocate                                          */
static void test_frag_merge(void) {
    enum {
        NUM = 16,
        SZ = 128
    };
    void *arr[NUM];
    int i;

    for (i = 0; i < NUM; ++i) {
        arr[i] = MR_NULL;
    }

    /* Allocate all */
    for (i = 0; i < NUM; i++) {
        arr[i] = mr_malloc(SZ);
        if (arr[i] == MR_NULL) {
            MR_TEST_EXPECT_NE_PTR(arr[i], MR_NULL);
            goto _exit;
        }
    }

    /* Free every second block to create holes */
    for (i = 0; i < NUM; i += 2) {
        mr_free(arr[i]);
        arr[i] = MR_NULL;
    }

    /* Re-allocate holes (should reuse freed blocks) */
    for (i = 0; i < NUM; i += 2) {
        arr[i] = mr_malloc(SZ);
        if (arr[i] == MR_NULL) {
            MR_TEST_EXPECT_NE_PTR(arr[i], MR_NULL);
            goto _exit;
        }
    }

    /* Clean up */
_exit:
    for (i = 0; i < NUM; ++i) {
        mr_free(arr[i]);
    }
}
MR_TEST_EXPORT(malloc, frag_merge, test_frag_merge);

/* -------------------------------------------------------------------------- */
/* Test: may fail gracefully if heap too small                                */
static void test_malloc_max(void) {
    const mr_size_t huge = 3 * 1024;
    mr_size_t usable;
    void *p = MR_NULL;

    p = mr_malloc(MR_SIZE_MAX >> 1);
    MR_TEST_EXPECT_EQ_PTR(p, MR_NULL);
    if (p != MR_NULL) {
        mr_free(p);
        p = MR_NULL;
    }

    p = mr_malloc(huge);
    if (p) {
#if defined(MR_USE_MALLOC)
        usable = mr_malloc_usable_size(p);
#else
        usable = huge;
#endif /* defined(MR_USE_MALLOC) */
        MR_TEST_EXPECT_GE_UINT(usable, huge);
        if (usable < huge) {
            goto _exit;
        }
        canary_fill(p, usable);
        MR_TEST_EXPECT_EQ_INT(canary_verify(p, usable), 1);
    }

_exit:
    mr_free(p);
}
MR_TEST_EXPORT(malloc, malloc_max, test_malloc_max);

/* -------------------------------------------------------------------------- */
/* Test: add memory region                                                    */
static void test_malloc_region(void) {
    static mr_uint8_t test_mem1[128];
    static mr_uint8_t test_mem2[256];
    const mr_heap_region_t regions[] = {
        {.address = test_mem1, .size = sizeof(test_mem1)},
        {.address = test_mem2, .size = sizeof(test_mem2)},
        {  .address = MR_NULL,                 .size = 0}
    };

    mr_heap_region_add(regions);
}
MR_TEST_EXPORT(malloc, malloc_region, test_malloc_region);
#endif /* defined(MR_USE_TEST) */
