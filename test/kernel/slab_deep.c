/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2026-08-30        MacRsh        First version
 */

#include <mrX/test/mr_test.h>
#if defined(MR_USE_TEST)
#include <mrX/kernel/mr_slab.h>

MR_TEST(slab, full_word_boundary) {
    mr_atomic_t map[1];
    mr_uint8_t buf[64 * 8];
    mr_slab_t slab;
    void *p;
    void *last;
    mr_uint32_t i;

    MR_TEST_ASSERT_EQ_UINT(mr_slab_init(&slab, map, sizeof(map), buf, 8, 64),
                           64);

    for (i = 0; i < 64; i++) {
        p = mr_slab_alloc(&slab);
        if (p == MR_NULL) {
            break;
        }
        MR_TEST_EXPECT_EQ_INT(mr_slab_ptr2index(&slab, p), (int)i);
    }
    MR_TEST_EXPECT_EQ_UINT(i, 64);
    MR_TEST_EXPECT_EQ_PTR(mr_slab_alloc(&slab), MR_NULL);

    /* The last object must be reusable after a free */
    last = mr_slab_index2ptr(&slab, 63);
    mr_slab_free(&slab, last);
    p = mr_slab_alloc(&slab);
    MR_TEST_EXPECT_EQ_PTR(p, last);
    mr_slab_free(&slab, p);
}

MR_TEST(slab, multi_word_map) {
    mr_atomic_t map[2];
    mr_uint8_t buf[70 * 16];
    mr_slab_t slab;
    void *p;
    void *q;
    mr_uint32_t i;

    MR_TEST_ASSERT_EQ_UINT(mr_slab_init(&slab, map, sizeof(map), buf, 16, 70),
                           70);

    for (i = 0; i < 70; i++) {
        p = mr_slab_alloc(&slab);
        if (p == MR_NULL) {
            break;
        }
        MR_TEST_EXPECT_EQ_INT(mr_slab_ptr2index(&slab, p), (int)i);
    }
    MR_TEST_EXPECT_EQ_UINT(i, 70);
    MR_TEST_EXPECT_EQ_PTR(mr_slab_alloc(&slab), MR_NULL);

    /* Free objects on both sides of the bitmap word boundary */
    mr_slab_free(&slab, mr_slab_index2ptr(&slab, 63));
    mr_slab_free(&slab, mr_slab_index2ptr(&slab, 64));
    p = mr_slab_alloc(&slab);
    q = mr_slab_alloc(&slab);
    MR_TEST_EXPECT_EQ_INT(mr_slab_ptr2index(&slab, p), 63);
    MR_TEST_EXPECT_EQ_INT(mr_slab_ptr2index(&slab, q), 64);
}

MR_TEST(slab, init_clamp_num) {
    mr_atomic_t map[1];
    mr_uint8_t buf[64 * 8];
    mr_slab_t slab;
    void *p;
    mr_uint32_t num;
    mr_uint32_t i;

    num = mr_slab_init(&slab, map, sizeof(map), buf, 8, 100);
    MR_TEST_EXPECT_EQ_UINT(num, 64);
    MR_TEST_EXPECT_EQ_UINT(slab.num, 64);
    MR_TEST_EXPECT_EQ_PTR(mr_slab_index2ptr(&slab, 64), MR_NULL);

    for (i = 0; i < 64; i++) {
        p = mr_slab_alloc(&slab);
        if (p == MR_NULL) {
            break;
        }
    }
    MR_TEST_EXPECT_EQ_UINT(i, 64);
    MR_TEST_EXPECT_EQ_PTR(mr_slab_alloc(&slab), MR_NULL);
    MR_TEST_EXPECT_EQ_INT(mr_slab_ptr2index(&slab, buf + (64 * 8)), -1);
}

MR_TEST(slab, init_extra_map_words) {
    mr_atomic_t map[2];
    mr_uint8_t buf[10 * 8];
    mr_slab_t slab;
    void *p;
    mr_uint32_t num;
    mr_uint32_t i;

    num = mr_slab_init(&slab, map, sizeof(map), buf, 8, 10);
    MR_TEST_EXPECT_EQ_UINT(num, 10);

    /* The partial last word mask must limit allocation to ten objects */
    for (i = 0; i < 10; i++) {
        p = mr_slab_alloc(&slab);
        if (p == MR_NULL) {
            break;
        }
        MR_TEST_EXPECT_EQ_INT(mr_slab_ptr2index(&slab, p), (int)i);
    }
    MR_TEST_EXPECT_EQ_UINT(i, 10);
    MR_TEST_EXPECT_EQ_PTR(mr_slab_alloc(&slab), MR_NULL);
}

MR_TEST(slab, init_zero_map_size) {
    mr_atomic_t map[1];
    mr_uint8_t buf[8];
    mr_slab_t slab;
    mr_uint32_t num;

    num = mr_slab_init(&slab, map, 0, buf, 8, 4);
    MR_TEST_EXPECT_EQ_UINT(num, 0);
    MR_TEST_EXPECT_EQ_UINT(slab.num, 0);
    MR_TEST_EXPECT_EQ_PTR(mr_slab_alloc(&slab), MR_NULL);
    MR_TEST_EXPECT_EQ_PTR(mr_slab_index2ptr(&slab, 0), MR_NULL);
    MR_TEST_EXPECT_EQ_INT(mr_slab_ptr2index(&slab, buf), -1);
}

MR_TEST(slab, zero_size_objects) {
    mr_atomic_t map[1];
    mr_uint8_t buf[8];
    mr_slab_t slab;
    void *p;
    void *q;

    MR_TEST_ASSERT_EQ_UINT(mr_slab_init(&slab, map, sizeof(map), buf, 0, 4), 4);

    p = mr_slab_alloc(&slab);
    q = mr_slab_alloc(&slab);
    MR_TEST_EXPECT_NE_PTR(p, MR_NULL);
    /* A zero stride maps every allocated slot to the buffer base */
    MR_TEST_EXPECT_EQ_PTR(p, buf);
    MR_TEST_EXPECT_EQ_PTR(q, buf);
    MR_TEST_EXPECT_NE_PTR(mr_slab_alloc(&slab), MR_NULL);
    MR_TEST_EXPECT_NE_PTR(mr_slab_alloc(&slab), MR_NULL);
    MR_TEST_EXPECT_EQ_PTR(mr_slab_alloc(&slab), MR_NULL);
    /* No free here, freeing would divide by the zero object size */
}

MR_TEST(slab, free_one_past_end) {
    mr_atomic_t map[1];
    mr_uint8_t buf[4 * 8];
    mr_slab_t slab;
    void *p;

    MR_TEST_ASSERT_EQ_UINT(mr_slab_init(&slab, map, sizeof(map), buf, 8, 4), 4);
    p = mr_slab_alloc(&slab);
    MR_TEST_ASSERT_NE_PTR(p, MR_NULL);
    if (p == MR_NULL) {
        return;
    }

    /* Freeing the one-past-the-end pointer must be ignored */
    mr_slab_free(&slab, buf + (4 * 8));
    MR_TEST_EXPECT_EQ_INT(mr_slab_ptr2index(&slab, mr_slab_alloc(&slab)), 1);
    /* Freeing an interior unaligned offset must be ignored too */
    mr_slab_free(&slab, buf + 3);
    MR_TEST_EXPECT_EQ_INT(mr_slab_ptr2index(&slab, mr_slab_alloc(&slab)), 2);
}

MR_TEST(slab, fragmentation_lowest_first) {
    mr_atomic_t map[1];
    mr_uint8_t buf[16 * 8];
    mr_slab_t slab;
    void *objects[16] = {MR_NULL};
    mr_uint32_t i;

    MR_TEST_ASSERT_EQ_UINT(mr_slab_init(&slab, map, sizeof(map), buf, 8, 16),
                           16);
    for (i = 0; i < 16; i++) {
        objects[i] = mr_slab_alloc(&slab);
        MR_TEST_ASSERT_NE_PTR(objects[i], MR_NULL);
    }
    for (i = 1; i < 16; i += 2) {
        mr_slab_free(&slab, objects[i]);
        objects[i] = MR_NULL;
    }

    /* The allocator must reuse the freed lowest indices in ascending order */
    for (i = 1; i < 16; i += 2) {
        objects[i] = mr_slab_alloc(&slab);
        MR_TEST_EXPECT_EQ_INT(mr_slab_ptr2index(&slab, objects[i]), (int)i);
    }
    MR_TEST_EXPECT_EQ_PTR(mr_slab_alloc(&slab), MR_NULL);
}

MR_TEST(slab, double_free_no_bit_corruption) {
    mr_atomic_t map[1];
    mr_uint8_t buf[4 * 8];
    mr_slab_t slab;
    void *p;

    MR_TEST_ASSERT_EQ_UINT(mr_slab_init(&slab, map, sizeof(map), buf, 8, 4), 4);
    p = mr_slab_alloc(&slab);
    MR_TEST_ASSERT_NE_PTR(p, MR_NULL);
    if (p == MR_NULL) {
        return;
    }

    mr_slab_free(&slab, p);
    mr_slab_free(&slab, p);
    /* The repeated free must not corrupt neighboring bitmap bits */
    MR_TEST_EXPECT_EQ_PTR(mr_slab_alloc(&slab), p);
    MR_TEST_EXPECT_EQ_INT(mr_slab_ptr2index(&slab, mr_slab_alloc(&slab)), 1);
    MR_TEST_EXPECT_EQ_INT(mr_slab_ptr2index(&slab, mr_slab_alloc(&slab)), 2);
    MR_TEST_EXPECT_EQ_INT(mr_slab_ptr2index(&slab, mr_slab_alloc(&slab)), 3);
    MR_TEST_EXPECT_EQ_PTR(mr_slab_alloc(&slab), MR_NULL);
}

MR_TEST(slab, stride_and_base) {
    mr_atomic_t map[1];
    mr_uint8_t buf[4 * 16];
    mr_slab_t slab;
    void *p;
    mr_uint32_t i;

    MR_TEST_ASSERT_EQ_UINT(mr_slab_init(&slab, map, sizeof(map), buf, 16, 4),
                           4);
    for (i = 0; i < 4; i++) {
        p = mr_slab_alloc(&slab);
        MR_TEST_ASSERT_NE_PTR(p, MR_NULL);
        MR_TEST_EXPECT_EQ_PTR(p, buf + (i * 16));
    }
}

MR_TEST(slab, ptr2index_rejections) {
    mr_atomic_t map[1];
    mr_uint8_t buf[4 * 8];
    mr_slab_t slab;
    void *p;

    MR_TEST_ASSERT_EQ_UINT(mr_slab_init(&slab, map, sizeof(map), buf, 8, 4), 4);
    p = mr_slab_alloc(&slab);
    MR_TEST_ASSERT_NE_PTR(p, MR_NULL);
    if (p == MR_NULL) {
        return;
    }

    MR_TEST_EXPECT_EQ_INT(mr_slab_ptr2index(&slab, buf + 4), -1);
    MR_TEST_EXPECT_EQ_INT(mr_slab_ptr2index(&slab, buf + (4 * 8)), -1);
    MR_TEST_EXPECT_EQ_INT(mr_slab_ptr2index(&slab, buf - 1), -1);
    MR_TEST_EXPECT_EQ_INT(mr_slab_ptr2index(&slab, p), 0);
    mr_slab_free(&slab, p);
}

MR_TEST(slab, one_byte_objects) {
    mr_atomic_t map[1];
    mr_uint8_t buf[8];
    mr_slab_t slab;
    void *p;
    mr_uint32_t i;

    MR_TEST_ASSERT_EQ_UINT(mr_slab_init(&slab, map, sizeof(map), buf, 1, 8), 8);
    for (i = 0; i < 8; i++) {
        p = mr_slab_alloc(&slab);
        if (p == MR_NULL) {
            break;
        }
        MR_TEST_EXPECT_EQ_PTR(p, buf + i);
    }
    MR_TEST_EXPECT_EQ_UINT(i, 8);
    MR_TEST_EXPECT_EQ_PTR(mr_slab_alloc(&slab), MR_NULL);
    mr_slab_free(&slab, buf + 7);
    MR_TEST_EXPECT_EQ_PTR(mr_slab_alloc(&slab), buf + 7);
}

#endif /* defined(MR_USE_TEST) */
