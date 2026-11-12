/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2026-08-30        MacRsh        First version
 */

#include <mrX/test/mr_test.h>
#if defined(MR_USE_TEST)
#if defined(MR_USE_MALLOC)
#include <mrX/libc/mr_malloc.h>

static void deep_fill(void *p, mr_size_t size, mr_uint8_t seed) {
    mr_uint8_t *data;
    mr_size_t i;

    for (data = (mr_uint8_t *)p, i = 0; i < size; ++i) {
        data[i] = (mr_uint8_t)(seed + i * 29U);
    }
}

static int deep_verify(void *p, mr_size_t size, mr_uint8_t seed) {
    mr_uint8_t *data;
    mr_size_t i;

    for (data = (mr_uint8_t *)p, i = 0; i < size; ++i) {
        if (data[i] != (mr_uint8_t)(seed + i * 29U)) {
            return 0;
        }
    }
    return 1;
}

MR_TEST(malloc, small_sizes_alignment) {
    static const mr_size_t sizes[]
        = {1, 2, 3, 7, 8, 9, 15, 16, 17, 31, 63, 64, 65, 95, 96, 97};
    void *p;
    mr_size_t usable, i;

    for (i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        p = mr_malloc(sizes[i]);
        if (p == MR_NULL) {
            MR_TEST_EXPECT_NE_PTR(p, MR_NULL);
            continue;
        }
        usable = mr_malloc_usable_size(p);
        MR_TEST_EXPECT_GE_UINT(usable, sizes[i]);
        /* Returned memory must be aligned to MR_ALIGN_SIZE */
        MR_TEST_EXPECT_EQ_UINT((mr_uintptr_t)p & (MR_ALIGN_SIZE - 1), 0);
        deep_fill(p, sizes[i], (mr_uint8_t)i);
        MR_TEST_EXPECT_EQ_INT(deep_verify(p, sizes[i], (mr_uint8_t)i), 1);
        mr_free(p);
    }
}

MR_TEST(malloc, blocks_no_overlap) {
    enum {
        NUM = 8,
        SZ = 8
    };
    void *arr[NUM];
    mr_size_t dup, i, j;

    for (i = 0; i < NUM; i++) {
        arr[i] = mr_malloc(SZ);
        if (arr[i] == MR_NULL) {
            MR_TEST_EXPECT_NE_PTR(arr[i], MR_NULL);
            arr[i] = MR_NULL;
        }
    }

    /* All pointers must be aligned */
    for (i = 0; i < NUM; i++) {
        if (arr[i] != MR_NULL) {
            MR_TEST_EXPECT_EQ_UINT((mr_uintptr_t)arr[i] & (MR_ALIGN_SIZE - 1),
                                   0);
        }
    }

    /* All pointers must be distinct */
    dup = 0;
    for (i = 0; i < NUM; i++) {
        for (j = i + 1; j < NUM; j++) {
            if ((arr[i] != MR_NULL) && (arr[j] != MR_NULL)
                && (arr[i] == arr[j])) {
                dup++;
            }
        }
    }
    MR_TEST_EXPECT_EQ_UINT(dup, 0);

    /* Each block keeps its own pattern */
    for (i = 0; i < NUM; i++) {
        if (arr[i] != MR_NULL) {
            deep_fill(arr[i], SZ, (mr_uint8_t)(0x40U + i));
        }
    }
    for (i = 0; i < NUM; i++) {
        if (arr[i] != MR_NULL) {
            MR_TEST_EXPECT_EQ_INT(
                deep_verify(arr[i], SZ, (mr_uint8_t)(0x40U + i)), 1);
        }
    }

    for (i = 0; i < NUM; i++) {
        mr_free(arr[i]);
    }
}

MR_TEST(malloc, edge_zero_requests) {
    void *p;

    p = mr_malloc(0);
    MR_TEST_EXPECT_EQ_PTR(p, MR_NULL);
    mr_free(p);

    MR_TEST_EXPECT_EQ_UINT(mr_malloc_usable_size(MR_NULL), 0);
    MR_TEST_EXPECT_EQ_UINT(mr_malloc_usable_size((void *)&p), 0);
}

MR_TEST(malloc, overflow_sizes) {
    void *p;

    /* Size alignment wraps to zero and must be rejected */
    p = mr_malloc(MR_SIZE_MAX);
    MR_TEST_EXPECT_EQ_PTR(p, MR_NULL);
    mr_free(p);

    p = mr_malloc(MR_SIZE_MAX - 7);
    MR_TEST_EXPECT_EQ_PTR(p, MR_NULL);
    mr_free(p);

    /* Aligned size stays huge and must fail the search */
    p = mr_malloc(MR_SIZE_MAX - 8);
    MR_TEST_EXPECT_EQ_PTR(p, MR_NULL);
    mr_free(p);

    p = mr_malloc(MR_SIZE_MAX >> 1);
    MR_TEST_EXPECT_EQ_PTR(p, MR_NULL);
    mr_free(p);
}

MR_TEST(malloc, free_null_noop) {
    mr_heap_info_t before;
    mr_heap_info_t after;

    before = mr_heap_information(MR_TRUE);
    mr_free(MR_NULL);
    after = mr_heap_information(MR_TRUE);
    MR_TEST_EXPECT_EQ_UINT(after.used_bytes, before.used_bytes);
    MR_TEST_EXPECT_EQ_UINT(after.free_bytes, before.free_bytes);
}

MR_TEST(malloc, calloc_edge_sizes) {
    void *p;

    p = mr_calloc(0, 8);
    MR_TEST_EXPECT_EQ_PTR(p, MR_NULL);
    mr_free(p);

    p = mr_calloc(8, 0);
    MR_TEST_EXPECT_EQ_PTR(p, MR_NULL);
    mr_free(p);

    p = mr_calloc(MR_SIZE_MAX, 0);
    MR_TEST_EXPECT_EQ_PTR(p, MR_NULL);
    mr_free(p);

    p = mr_calloc(MR_SIZE_MAX, 2);
    MR_TEST_EXPECT_EQ_PTR(p, MR_NULL);
    mr_free(p);

    p = mr_calloc(2, MR_SIZE_MAX);
    MR_TEST_EXPECT_EQ_PTR(p, MR_NULL);
    mr_free(p);

    p = mr_calloc(MR_SIZE_MAX, MR_SIZE_MAX);
    MR_TEST_EXPECT_EQ_PTR(p, MR_NULL);
    mr_free(p);
}

MR_TEST(malloc, calloc_zero_full_range) {
    enum {
        SZ = 512
    };
    void *p;
    mr_size_t usable, i;

    p = mr_calloc(8, SZ / 8);
    if (p == MR_NULL) {
        MR_TEST_EXPECT_NE_PTR(p, MR_NULL);
        return;
    }
    usable = mr_malloc_usable_size(p);
    MR_TEST_EXPECT_GE_UINT(usable, SZ);

    for (i = 0; i < SZ; i++) {
        MR_TEST_EXPECT_EQ_INT(((mr_uint8_t *)p)[i], 0);
    }
    mr_free(p);
}

MR_TEST(malloc, realloc_zero_frees) {
    mr_heap_info_t before;
    mr_heap_info_t after;
    void *p, *q;

    /* realloc(MR_NULL, 0) must be a safe NULL */
    q = mr_realloc(MR_NULL, 0);
    MR_TEST_EXPECT_EQ_PTR(q, MR_NULL);

    before = mr_heap_information(MR_TRUE);
    p = mr_malloc(64);
    if (p == MR_NULL) {
        MR_TEST_EXPECT_NE_PTR(p, MR_NULL);
        return;
    }
    deep_fill(p, 64, 0x11);

    /* Size zero frees the block and returns NULL (implementation defined
     * return value, but the memory must not leak) */
    q = mr_realloc(p, 0);
    p = MR_NULL;
    MR_TEST_EXPECT_EQ_PTR(q, MR_NULL);
    after = mr_heap_information(MR_TRUE);
    MR_TEST_EXPECT_EQ_UINT(after.used_bytes, before.used_bytes);
    MR_TEST_EXPECT_EQ_UINT(after.free_bytes, before.free_bytes);
}

MR_TEST(malloc, realloc_grow_shrink_prefix) {
    void *p, *q, *r;

    p = mr_malloc(32);
    if (p == MR_NULL) {
        MR_TEST_EXPECT_NE_PTR(p, MR_NULL);
        return;
    }
    deep_fill(p, 32, 0x21);

    q = mr_realloc(p, 320);
    if (q == MR_NULL) {
        /* realloc failure must keep the old block valid */
        MR_TEST_EXPECT_NE_PTR(q, MR_NULL);
        MR_TEST_EXPECT_EQ_INT(deep_verify(p, 32, 0x21), 1);
        mr_free(p);
        return;
    }
    p = MR_NULL;
    MR_TEST_EXPECT_EQ_INT(deep_verify(q, 32, 0x21), 1);
    MR_TEST_EXPECT_GE_UINT(mr_malloc_usable_size(q), 320);
    deep_fill(q, 320, 0x22);

    r = mr_realloc(q, 48);
    if (r == MR_NULL) {
        MR_TEST_EXPECT_NE_PTR(r, MR_NULL);
        mr_free(q);
        return;
    }
    q = MR_NULL;
    /* Only the first min(old, new) bytes are guaranteed */
    MR_TEST_EXPECT_EQ_INT(deep_verify(r, 48, 0x22), 1);
    mr_free(r);
}

MR_TEST(malloc, realloc_chain_growth) {
    mr_size_t size, old, i, k;
    void *p, *q;

    size = 16;
    p = mr_malloc(size);
    if (p == MR_NULL) {
        MR_TEST_EXPECT_NE_PTR(p, MR_NULL);
        return;
    }
    deep_fill(p, size, 0x33);

    for (i = 0; i < 7; i++) {
        old = size;
        size = size * 2;
        q = mr_realloc(p, size);
        if (q == MR_NULL) {
            MR_TEST_EXPECT_NE_PTR(q, MR_NULL);
            mr_free(p);
            return;
        }
        MR_TEST_EXPECT_EQ_INT(deep_verify(q, old, 0x33), 1);
        /* Extend the pattern over the newly grown region, the verify
           helper indexes the pattern by absolute position so the fill
           must continue the sequence instead of restarting it */
        for (k = old; k < size; k++) {
            ((mr_uint8_t *)q)[k] = (mr_uint8_t)(0x33 + k * 29U);
        }
        p = q;
    }
    MR_TEST_EXPECT_EQ_INT(deep_verify(p, size, 0x33), 1);
    mr_free(p);
}

MR_TEST(malloc, frag_coalesce_restore) {
    mr_heap_info_t base;
    mr_heap_info_t end;
    void *a = MR_NULL, *b = MR_NULL, *c = MR_NULL;

    base = mr_heap_information(MR_TRUE);
    a = mr_malloc(128);
    b = mr_malloc(128);
    c = mr_malloc(128);
    if ((a == MR_NULL) || (b == MR_NULL) || (c == MR_NULL)) {
        MR_TEST_EXPECT_NE_PTR(a, MR_NULL);
        MR_TEST_EXPECT_NE_PTR(b, MR_NULL);
        MR_TEST_EXPECT_NE_PTR(c, MR_NULL);
        goto _exit;
    }

    /* Free in scattered order to exercise both side merges */
    mr_free(b);
    b = MR_NULL;
    mr_free(a);
    a = MR_NULL;
    mr_free(c);
    c = MR_NULL;

    end = mr_heap_information(MR_TRUE);
    MR_TEST_EXPECT_EQ_UINT(end.used_bytes, base.used_bytes);
    MR_TEST_EXPECT_EQ_UINT(end.free_bytes, base.free_bytes);
    /* All fragments must merge back into the original free block */
    MR_TEST_EXPECT_EQ_UINT(end.frag_num, base.frag_num);

_exit:
    mr_free(a);
    mr_free(b);
    mr_free(c);
}

MR_TEST(malloc, heap_info_invariants) {
    mr_heap_info_t before;
    mr_heap_info_t during;
    mr_heap_info_t after;
    void *p;

    before = mr_heap_information(MR_FALSE);
    MR_TEST_EXPECT_EQ_UINT(before.used_bytes + before.free_bytes,
                           before.total_bytes);
    MR_TEST_EXPECT_GE_UINT(before.free_bytes, before.max_free_bytes);
    MR_TEST_EXPECT_LE_UINT(before.frag_ratio, 1000);
    MR_TEST_EXPECT_LE_UINT(before.disp_kb_ratio, 1000);

    p = mr_malloc(512);
    if (p == MR_NULL) {
        MR_TEST_EXPECT_NE_PTR(p, MR_NULL);
        return;
    }
    during = mr_heap_information(MR_TRUE);
    MR_TEST_EXPECT_EQ_UINT(during.total_bytes, before.total_bytes);
    /* Used growth must match free drop exactly */
    MR_TEST_EXPECT_EQ_UINT(during.used_bytes - before.used_bytes,
                           before.free_bytes - during.free_bytes);

    mr_free(p);
    after = mr_heap_information(MR_TRUE);
    MR_TEST_EXPECT_EQ_UINT(after.used_bytes, before.used_bytes);
    MR_TEST_EXPECT_EQ_UINT(after.free_bytes, before.free_bytes);
}

MR_TEST(malloc, fail_count_tracks) {
    mr_heap_info_t before;
    mr_heap_info_t after;
    void *p;

    before = mr_heap_information(MR_TRUE);
    p = mr_malloc(MR_SIZE_MAX >> 1);
    MR_TEST_EXPECT_EQ_PTR(p, MR_NULL);
    mr_free(p);

    after = mr_heap_information(MR_TRUE);
    MR_TEST_EXPECT_EQ_UINT(after.fail_count, before.fail_count + 1);
}

MR_TEST(malloc, usable_region_writable) {
    void *p;
    mr_size_t usable;

    p = mr_malloc(200);
    if (p == MR_NULL) {
        MR_TEST_EXPECT_NE_PTR(p, MR_NULL);
        return;
    }
    usable = mr_malloc_usable_size(p);
    MR_TEST_EXPECT_GE_UINT(usable, 200);
    deep_fill(p, usable, 0x77);
    MR_TEST_EXPECT_EQ_INT(deep_verify(p, usable, 0x77), 1);
    mr_free(p);

    /* Magic check makes the freed block report zero usable size */
    MR_TEST_EXPECT_EQ_UINT(mr_malloc_usable_size(p), 0);
}

MR_TEST(malloc, alloc_free_churn) {
    enum {
        NUM = 24
    };
    void *arr[NUM];
    mr_size_t size, i;

    for (i = 0; i < NUM; i++) {
        arr[i] = MR_NULL;
    }

    for (i = 0; i < NUM; i++) {
        size = 16 * (i % 5 + 1);
        arr[i] = mr_malloc(size);
        if (arr[i] == MR_NULL) {
            MR_TEST_EXPECT_NE_PTR(arr[i], MR_NULL);
            goto _exit;
        }
        deep_fill(arr[i], size, (mr_uint8_t)(i * 3U + 7U));
    }

    /* Punch holes */
    for (i = 0; i < NUM; i += 3) {
        mr_free(arr[i]);
        arr[i] = MR_NULL;
    }

    /* Refill holes with different sizes and patterns */
    for (i = 0; i < NUM; i += 3) {
        size = 32 * (i % 4 + 1);
        arr[i] = mr_malloc(size);
        if (arr[i] == MR_NULL) {
            MR_TEST_EXPECT_NE_PTR(arr[i], MR_NULL);
            goto _exit;
        }
        deep_fill(arr[i], size, (mr_uint8_t)(i * 5U + 11U));
    }

    /* Every live block must still hold its own pattern */
    for (i = 0; i < NUM; i++) {
        if (i % 3 != 0) {
            size = 16 * (i % 5 + 1);
        } else {
            size = 32 * (i % 4 + 1);
        }
        if (arr[i] != MR_NULL) {
            MR_TEST_EXPECT_EQ_INT(
                deep_verify(
                    arr[i], size,
                    (mr_uint8_t)(i % 3 != 0 ? i * 3U + 7U : i * 5U + 11U)),
                1);
        }
    }

_exit:
    for (i = 0; i < NUM; i++) {
        mr_free(arr[i]);
    }
}

#endif /* defined(MR_USE_MALLOC) */
#endif /* defined(MR_USE_TEST) */
