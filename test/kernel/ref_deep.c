/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2026-08-30        MacRsh        First version
 */

#include <mrX/test/mr_test.h>
#if defined(MR_USE_TEST)
#include <mrX/kernel/mr_ref.h>

/* Helpers */
static int test_ref_deep_release_calls;
static int test_ref_deep_release_inited;
static int test_ref_deep_release_count;

static void test_ref_deep_release_reinit(mr_ref_t *ref) {
    test_ref_deep_release_calls++;
    test_ref_deep_release_inited = MR_REF_IS_INITED(*ref);
    test_ref_deep_release_count = (int)MR_REF_COUNT(*ref);
    mr_ref_init(ref);
}

static void test_ref_deep_release_marker(mr_ref_t *ref) {
    test_ref_deep_release_calls++;
    *ref = 0xdeadbeef;
}

/* Tests */
#if defined(MR_USE_REF_LONG_COUNT)
MR_TEST(ref, long_count_layout) {
    mr_ref_t r;

    MR_TEST_ASSERT_EQ_UINT(MR_REF_MAGIC, 0x6d720000U);
    MR_TEST_ASSERT_EQ_UINT(MR_REF_MAGIC_MASK, 0xffff0000U);
    MR_TEST_ASSERT_EQ_UINT(MR_REF_OVERFLOW, 0x6d730000U);
    mr_ref_init(&r);
    MR_TEST_EXPECT_EQ_UINT((mr_uint64_t)r, 0x6d720001U);
    MR_TEST_EXPECT_EQ_INT(MR_REF_IS_INITED(r), MR_TRUE);
    MR_TEST_EXPECT_EQ_INT(MR_REF_COUNT(r), 1);
}
#endif /* defined(MR_USE_REF_LONG_COUNT) */

MR_TEST(ref, count_max_roundtrip) {
    mr_ref_t r;
    mr_uint32_t max_count;
    mr_uint32_t i;

    test_ref_deep_release_calls = 0;
    mr_ref_init(&r);
    max_count = (mr_uint32_t)(MR_REF_OVERFLOW - MR_REF_MAGIC - 1U);

    /* Increment up to the configured maximum count without crossing it */
    for (i = 0; i < (max_count - 1U); i++) {
        if (mr_ref_get(&r) != MR_TRUE) {
            break;
        }
    }
    MR_TEST_EXPECT_EQ_UINT(i, max_count - 1U);
    MR_TEST_EXPECT_EQ_UINT((mr_uint64_t)MR_REF_COUNT(r), max_count);
    MR_TEST_EXPECT_EQ_INT(MR_REF_IS_INITED(r), MR_TRUE);

    /* Decrement back to one reference without releasing */
    for (i = 0; i < (max_count - 1U); i++) {
        if (mr_ref_put(&r, MR_NULL) != MR_FALSE) {
            break;
        }
    }
    MR_TEST_EXPECT_EQ_UINT(i, max_count - 1U);
    MR_TEST_EXPECT_EQ_INT(MR_REF_IS_INITED(r), MR_TRUE);
    MR_TEST_EXPECT_EQ_INT(MR_REF_COUNT(r), 1);
    MR_TEST_EXPECT_EQ_INT(mr_ref_put(&r, test_ref_deep_release_reinit),
                          MR_TRUE);
    MR_TEST_EXPECT_EQ_INT(test_ref_deep_release_calls, 1);
}

MR_TEST(ref, get_overflow_crossing) {
    mr_ref_t r;
    mr_uint32_t max_count;
    mr_uint32_t i;

    mr_ref_init(&r);
    max_count = (mr_uint32_t)(MR_REF_OVERFLOW - MR_REF_MAGIC - 1U);
    for (i = 0; i < (max_count - 1U); i++) {
        if (mr_ref_get(&r) != MR_TRUE) {
            break;
        }
    }
    MR_TEST_EXPECT_EQ_UINT(i, max_count - 1U);
    MR_TEST_EXPECT_EQ_UINT((mr_uint64_t)MR_REF_COUNT(r), max_count);

    /* Crossing the maximum count intentionally invalidates the ref */
    MR_TEST_EXPECT_EQ_INT(mr_ref_get(&r), MR_TRUE);
    MR_TEST_EXPECT_EQ_INT(MR_REF_IS_INITED(r), MR_FALSE);
    MR_TEST_EXPECT_EQ_UINT((mr_uint64_t)mr_atomic_load(&r),
                           (mr_uint64_t)MR_REF_OVERFLOW);
}

MR_TEST(ref, put_null_last_lifecycle) {
    mr_ref_t r;

    mr_ref_init(&r);
    /* A null release drains the count without ending the marked lifecycle */
    MR_TEST_EXPECT_EQ_INT(mr_ref_put(&r, MR_NULL), MR_FALSE);
    MR_TEST_EXPECT_EQ_INT(MR_REF_IS_INITED(r), MR_TRUE);
    MR_TEST_EXPECT_EQ_INT(MR_REF_COUNT(r), 0);
    MR_TEST_EXPECT_EQ_INT(mr_ref_get(&r), MR_FALSE);
}

MR_TEST(ref, put_below_zero_magic_underflow) {
    mr_ref_t r;

    mr_ref_init(&r);
    mr_ref_put(&r, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(MR_REF_COUNT(r), 0);

    /* Putting a zero count underflows and invalidates the magic field */
    MR_TEST_EXPECT_EQ_INT(mr_ref_put(&r, MR_NULL), MR_FALSE);
    MR_TEST_EXPECT_EQ_INT(MR_REF_IS_INITED(r), MR_FALSE);
    MR_TEST_EXPECT_EQ_INT(mr_ref_get(&r), MR_FALSE);
}

MR_TEST(ref, release_callback_and_reuse) {
    mr_ref_t r;

    test_ref_deep_release_calls = 0;
    mr_ref_init(&r);
    MR_TEST_ASSERT_EQ_INT(mr_ref_put(&r, test_ref_deep_release_reinit),
                          MR_TRUE);
    MR_TEST_EXPECT_EQ_INT(test_ref_deep_release_calls, 1);
    /* The callback must observe the cleared magic and a zero count */
    MR_TEST_EXPECT_EQ_INT(test_ref_deep_release_inited, MR_FALSE);
    MR_TEST_EXPECT_EQ_INT(test_ref_deep_release_count, 0);

    /* The ref re-initialized by the callback must be reusable */
    MR_TEST_EXPECT_EQ_INT(mr_ref_get(&r), MR_TRUE);
    MR_TEST_EXPECT_EQ_INT(MR_REF_COUNT(r), 2);
    MR_TEST_EXPECT_EQ_INT(mr_ref_put(&r, MR_NULL), MR_FALSE);
    MR_TEST_EXPECT_EQ_INT(mr_ref_put(&r, test_ref_deep_release_reinit),
                          MR_TRUE);
    MR_TEST_EXPECT_EQ_INT(test_ref_deep_release_calls, 2);
}

MR_TEST(ref, release_not_recalled_after_death) {
    mr_ref_t r;
    int i;

    test_ref_deep_release_calls = 0;
    mr_ref_init(&r);
    MR_TEST_ASSERT_EQ_INT(mr_ref_put(&r, test_ref_deep_release_marker),
                          MR_TRUE);
    MR_TEST_ASSERT_EQ_INT(test_ref_deep_release_calls, 1);

    /* Further puts on the dead ref must not invoke the release again */
    for (i = 0; i < 3; i++) {
        MR_TEST_EXPECT_EQ_INT(mr_ref_put(&r, test_ref_deep_release_marker),
                              MR_FALSE);
    }
    MR_TEST_EXPECT_EQ_INT(test_ref_deep_release_calls, 1);
}

MR_TEST(ref, zero_inited_ref) {
    mr_ref_t r;

    mr_atomic_init(&r, 0);
    MR_TEST_EXPECT_EQ_INT(MR_REF_IS_INITED(r), MR_FALSE);
    MR_TEST_EXPECT_EQ_INT(mr_ref_get(&r), MR_FALSE);
    MR_TEST_EXPECT_EQ_INT(mr_ref_put(&r, test_ref_deep_release_reinit),
                          MR_FALSE);

    /* The dead ref must stay dead after rejected operations */
    MR_TEST_EXPECT_EQ_INT(MR_REF_IS_INITED(r), MR_FALSE);
    MR_TEST_EXPECT_EQ_INT(mr_ref_get(&r), MR_FALSE);
}

MR_TEST(ref, interleaved_balance) {
    mr_ref_t r;

    test_ref_deep_release_calls = 0;
    mr_ref_init(&r);
    MR_TEST_ASSERT_EQ_INT(MR_REF_COUNT(r), 1);
    MR_TEST_ASSERT_EQ_INT(mr_ref_get(&r), MR_TRUE);
    MR_TEST_ASSERT_EQ_INT(mr_ref_get(&r), MR_TRUE);
    MR_TEST_ASSERT_EQ_INT(MR_REF_COUNT(r), 3);
    MR_TEST_ASSERT_EQ_INT(mr_ref_put(&r, MR_NULL), MR_FALSE);
    MR_TEST_ASSERT_EQ_INT(MR_REF_COUNT(r), 2);
    MR_TEST_ASSERT_EQ_INT(mr_ref_get(&r), MR_TRUE);
    MR_TEST_ASSERT_EQ_INT(MR_REF_COUNT(r), 3);
    MR_TEST_ASSERT_EQ_INT(mr_ref_put(&r, MR_NULL), MR_FALSE);
    MR_TEST_ASSERT_EQ_INT(mr_ref_put(&r, test_ref_deep_release_marker),
                          MR_FALSE);
    MR_TEST_ASSERT_EQ_INT(MR_REF_COUNT(r), 1);
    MR_TEST_EXPECT_EQ_INT(mr_ref_put(&r, test_ref_deep_release_marker),
                          MR_TRUE);
    MR_TEST_EXPECT_EQ_INT(test_ref_deep_release_calls, 1);
}

MR_TEST(ref, get_on_dead_marker_ref) {
    mr_ref_t r;

    test_ref_deep_release_calls = 0;
    mr_ref_init(&r);
    MR_TEST_ASSERT_EQ_INT(mr_ref_put(&r, test_ref_deep_release_marker),
                          MR_TRUE);
    MR_TEST_ASSERT_EQ_INT(test_ref_deep_release_calls, 1);

    /* Repeated gets on the dead marker value must keep failing */
    MR_TEST_EXPECT_EQ_INT(mr_ref_get(&r), MR_FALSE);
    MR_TEST_EXPECT_EQ_INT(mr_ref_get(&r), MR_FALSE);
}

#endif /* defined(MR_USE_TEST) */
