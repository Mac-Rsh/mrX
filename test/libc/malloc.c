/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/test/mr_test.h>
#if defined(MR_USE_TEST)
#if defined(MR_USE_MALLOC)
#include <mrX/libc/mr_malloc.h>

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

/* Test: allocate, write pattern, verify, free */
MR_TEST(malloc, free_basic) {
    mr_size_t usable;
    void *p = MR_NULL;

    p = mr_malloc(0);
    MR_TEST_EXPECT_EQ_PTR(p, MR_NULL);
    if (p != MR_NULL) {
        mr_free(p);
        p = MR_NULL;
    }
    mr_free(MR_NULL);

    p = mr_malloc(128);
    if (p == MR_NULL) {
        MR_TEST_EXPECT_NE_PTR(p, MR_NULL);
        goto _exit;
    }

    usable = mr_malloc_usable_size(p);
    MR_TEST_EXPECT_GE_UINT(usable, 128);
    if (usable < 128) {
        goto _exit;
    }

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

_exit:
    mr_free(p);
}

/* Test: memory must be zero-filled                                           */
MR_TEST(malloc, calloc_zero) {
    const mr_size_t n = 16, sz = 32;
    mr_size_t usable, i;
    void *p = MR_NULL;

    p = mr_calloc(0, 0);
    MR_TEST_EXPECT_EQ_PTR(p, MR_NULL);
    if (p != MR_NULL) {
        mr_free(p);
        p = MR_NULL;
    }

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

    usable = mr_malloc_usable_size(p);
    MR_TEST_EXPECT_GE_UINT(usable, n * sz);
    if (usable < (n * sz)) {
        goto _exit;
    }

    /* Verify all bytes are zero */
    for (i = 0; i < usable; ++i) {
        MR_TEST_EXPECT_EQ_INT(((mr_uint8_t *)p)[i], 0);
    }

_exit:
    mr_free(p);
}

/* Test: resize buffer and retain original data                               */
MR_TEST(malloc, realloc_resize) {
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

/* Test: Zero-size malloc should return NULL                                  */
MR_TEST(malloc, malloc_zero) {
    void *p;

    p = mr_malloc(0);
    MR_TEST_EXPECT_EQ_PTR(p, MR_NULL);
    mr_free(p);
}


/* Test: allocate, free, re-allocate                                          */
MR_TEST(malloc, frag_merge) {
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

/* Test: may fail gracefully if heap too small                                */
MR_TEST(malloc, malloc_max) {
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
    if (p != MR_NULL) {
        usable = mr_malloc_usable_size(p);
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

/* Test: add memory region                                                    */
MR_TEST(malloc, malloc_region) {
    static mr_uint8_t test_mem1[128];
    static mr_uint8_t test_mem2[256];
    mr_heap_info_t before;
    mr_heap_info_t after;
    const mr_heap_region_t regions[] = {
        {.address = test_mem1, .size = sizeof(test_mem1)},
        {.address = test_mem2, .size = sizeof(test_mem2)},
        {  .address = MR_NULL,                 .size = 0}
    };

    before = mr_heap_information(MR_TRUE);
    mr_heap_region_add(regions);
    after = mr_heap_information(MR_TRUE);
    MR_TEST_EXPECT_GT_UINT(after.total_bytes, before.total_bytes);
    MR_TEST_EXPECT_GT_UINT(after.free_bytes, before.free_bytes);
}

/* Test: a region below the minimum size is rejected without changes         */
MR_TEST(malloc, region_too_small_skipped) {
    static mr_uint8_t tiny_mem[64];
    mr_heap_info_t before;
    mr_heap_info_t after;
    const mr_heap_region_t regions[] = {
        {.address = tiny_mem, .size = sizeof(tiny_mem)},
        {.address = MR_NULL,  .size = 0}
    };

    before = mr_heap_information(MR_TRUE);
    mr_heap_region_add(regions);
    after = mr_heap_information(MR_TRUE);
    MR_TEST_EXPECT_EQ_UINT(after.total_bytes, before.total_bytes);
    MR_TEST_EXPECT_EQ_UINT(after.free_bytes, before.free_bytes);
    MR_TEST_EXPECT_EQ_UINT(after.frag_num, before.frag_num);
}

/* Test: realloc(NULL, size) behaves like malloc                              */
MR_TEST(malloc, realloc_null) {
    void *p = MR_NULL;

    p = mr_realloc(MR_NULL, 64);
    if (p == MR_NULL) {
        MR_TEST_EXPECT_NE_PTR(p, MR_NULL);
        return;
    }
    canary_fill(p, 64);
    MR_TEST_EXPECT_EQ_INT(canary_verify(p, 64), 1);
    mr_free(p);
}

/* Test: heap information tracks used and free bytes                          */
MR_TEST(malloc, heap_information) {
    mr_heap_info_t before;
    mr_heap_info_t after;
    mr_heap_info_t restored;
    void *p = MR_NULL;

    before = mr_heap_information(MR_FALSE);
    MR_TEST_ASSERT_GE_UINT(before.total_bytes, before.free_bytes);
    MR_TEST_ASSERT_GE_UINT(before.free_bytes, before.max_free_bytes);

    p = mr_malloc(256);
    if (p == MR_NULL) {
        MR_TEST_EXPECT_NE_PTR(p, MR_NULL);
        return;
    }

    after = mr_heap_information(MR_TRUE);
    MR_TEST_EXPECT_GT_UINT(after.used_bytes, before.used_bytes);
    MR_TEST_EXPECT_LT_UINT(after.free_bytes, before.free_bytes);
    MR_TEST_EXPECT_EQ_UINT(after.total_bytes, before.total_bytes);

    mr_free(p);
    restored = mr_heap_information(MR_TRUE);
    MR_TEST_EXPECT_EQ_UINT(restored.used_bytes, before.used_bytes);
    MR_TEST_EXPECT_EQ_UINT(restored.free_bytes, before.free_bytes);
}

/* Test: realloc of a live block keeps identity of the data prefix            */
MR_TEST(malloc, realloc_same_size) {
    void *p1 = MR_NULL;
    void *p2 = MR_NULL;

    p1 = mr_malloc(48);
    if (p1 == MR_NULL) {
        MR_TEST_EXPECT_NE_PTR(p1, MR_NULL);
        return;
    }
    canary_fill(p1, 48);

    p2 = mr_realloc(p1, 48);
    if (p2 == MR_NULL) {
        MR_TEST_EXPECT_NE_PTR(p2, MR_NULL);
        mr_free(p1);
        return;
    }
    MR_TEST_EXPECT_EQ_INT(canary_verify(p2, 48), 1);
    mr_free(p2);
}

#endif /* defined(MR_USE_MALLOC) */
#endif /* defined(MR_USE_TEST) */
