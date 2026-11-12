/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2026-08-30        MacRsh        First version
 */

#include <mrX/test/mr_test.h>
#if defined(MR_USE_TEST)
#if defined(MR_USE_ATOMIC)
#include <mrX/libc/mr_atomic.h>

MR_TEST(atomic, min_max_store_load) {
    mr_atomic_t v;

    mr_atomic_init(&v, 0);
    mr_atomic_store(&v, MR_INT64_MAX);
    MR_TEST_EXPECT_EQ_INT(mr_atomic_load(&v), MR_INT64_MAX);
    mr_atomic_store(&v, MR_INT64_MIN);
    MR_TEST_EXPECT_EQ_INT(mr_atomic_load(&v), MR_INT64_MIN);
    mr_atomic_store(&v, 0);
    MR_TEST_EXPECT_EQ_INT(mr_atomic_load(&v), 0);
}

MR_TEST(atomic, init_macro_values) {
    mr_atomic_t a = MR_ATOMIC_INIT(-1);
    mr_atomic_t b = MR_ATOMIC_INIT(0);
    mr_atomic_t c = MR_ATOMIC_INIT(MR_INT64_MAX);

    MR_TEST_EXPECT_EQ_INT(mr_atomic_load(&a), -1);
    MR_TEST_EXPECT_EQ_INT(mr_atomic_load(&b), 0);
    MR_TEST_EXPECT_EQ_INT(mr_atomic_load(&c), MR_INT64_MAX);
}

MR_TEST(atomic, exchange_extremes) {
    mr_atomic_t v, old;

    mr_atomic_init(&v, MR_INT64_MAX);
    old = mr_atomic_exchange(&v, MR_INT64_MIN);
    MR_TEST_EXPECT_EQ_INT(old, MR_INT64_MAX);
    MR_TEST_EXPECT_EQ_INT(mr_atomic_load(&v), MR_INT64_MIN);

    old = mr_atomic_exchange(&v, 0);
    MR_TEST_EXPECT_EQ_INT(old, MR_INT64_MIN);
    MR_TEST_EXPECT_EQ_INT(mr_atomic_load(&v), 0);
}

MR_TEST(atomic, cas_success_keeps_expect) {
    mr_atomic_t v, expect;

    mr_atomic_init(&v, 5);
    expect = 5;
    MR_TEST_EXPECT_EQ_INT(mr_atomic_compare_exchange(&v, &expect, 6), MR_TRUE);
    MR_TEST_EXPECT_EQ_INT(mr_atomic_load(&v), 6);
    /* The expected value must not be modified on success */
    MR_TEST_EXPECT_EQ_INT(expect, 5);
}

MR_TEST(atomic, cas_failure_refreshes_expect) {
    mr_atomic_t v, expect;

    mr_atomic_init(&v, 7);
    expect = 8;
    MR_TEST_EXPECT_EQ_INT(mr_atomic_compare_exchange(&v, &expect, 9), MR_FALSE);
    /* The atomic must keep its value on failure */
    MR_TEST_EXPECT_EQ_INT(mr_atomic_load(&v), 7);
    /* The expected value must be refreshed with the current value */
    MR_TEST_EXPECT_EQ_INT(expect, 7);

    /* The refreshed expected value makes the next attempt succeed */
    MR_TEST_EXPECT_EQ_INT(mr_atomic_compare_exchange(&v, &expect, 9), MR_TRUE);
    MR_TEST_EXPECT_EQ_INT(mr_atomic_load(&v), 9);
}

MR_TEST(atomic, cas_extreme_patterns) {
    mr_atomic_t v, expect;

    mr_atomic_init(&v, MR_INT64_MIN);
    expect = MR_INT64_MIN;
    /* Only the desired value is stored, no arithmetic happens */
    MR_TEST_EXPECT_EQ_INT(mr_atomic_compare_exchange(&v, &expect, MR_INT64_MAX),
                          MR_TRUE);
    MR_TEST_EXPECT_EQ_INT(mr_atomic_load(&v), MR_INT64_MAX);

    expect = 0;
    MR_TEST_EXPECT_EQ_INT(mr_atomic_compare_exchange(&v, &expect, 1), MR_FALSE);
    MR_TEST_EXPECT_EQ_INT(expect, MR_INT64_MAX);
}

MR_TEST(atomic, cas_chain) {
    mr_atomic_t v, expect;
    int i;

    mr_atomic_init(&v, 0);
    for (i = 0; i < 8; i++) {
        expect = i;
        MR_TEST_EXPECT_EQ_INT(mr_atomic_compare_exchange(&v, &expect, i + 1),
                              MR_TRUE);
    }
    MR_TEST_EXPECT_EQ_INT(mr_atomic_load(&v), 8);
}

MR_TEST(atomic, fetch_add_zero_and_extremes) {
    mr_atomic_t v;

    mr_atomic_init(&v, 42);
    MR_TEST_EXPECT_EQ_INT(mr_atomic_fetch_add(&v, 0), 42);
    MR_TEST_EXPECT_EQ_INT(mr_atomic_load(&v), 42);

    mr_atomic_store(&v, MR_INT64_MAX);
    MR_TEST_EXPECT_EQ_INT(mr_atomic_fetch_add(&v, 0), MR_INT64_MAX);
    MR_TEST_EXPECT_EQ_INT(mr_atomic_load(&v), MR_INT64_MAX);
}

MR_TEST(atomic, add_sub_negative_equivalence) {
    mr_atomic_t v;

    mr_atomic_init(&v, 100);
    MR_TEST_EXPECT_EQ_INT(mr_atomic_fetch_add(&v, -30), 100);
    MR_TEST_EXPECT_EQ_INT(mr_atomic_load(&v), 70);
    MR_TEST_EXPECT_EQ_INT(mr_atomic_fetch_sub(&v, -30), 70);
    MR_TEST_EXPECT_EQ_INT(mr_atomic_load(&v), 100);

    /* Sub of a positive value matches add of a negative value */
    MR_TEST_EXPECT_EQ_INT(mr_atomic_fetch_sub(&v, 25), 100);
    MR_TEST_EXPECT_EQ_INT(mr_atomic_load(&v), 75);
    MR_TEST_EXPECT_EQ_INT(mr_atomic_fetch_add(&v, -25), 75);
    MR_TEST_EXPECT_EQ_INT(mr_atomic_load(&v), 50);
}

MR_TEST(atomic, fetch_wrap_extremes) {
    mr_atomic_t v;

    mr_atomic_init(&v, MR_INT64_MAX);
    /* Two's complement wrap is the expected behavior on this platform,
       adding 1 to the maximum wraps to the minimum */
    MR_TEST_EXPECT_EQ_INT(mr_atomic_fetch_add(&v, 1), MR_INT64_MAX);
    MR_TEST_EXPECT_EQ_INT(mr_atomic_load(&v), MR_INT64_MIN);
    MR_TEST_EXPECT_EQ_INT(mr_atomic_fetch_sub(&v, 1), MR_INT64_MIN);
    MR_TEST_EXPECT_EQ_INT(mr_atomic_load(&v), MR_INT64_MAX);
}

MR_TEST(atomic, bitwise_extremes) {
    mr_atomic_t v;

    /* AND with zero clears everything */
    mr_atomic_init(&v, MR_INT64_MAX);
    MR_TEST_EXPECT_EQ_INT(mr_atomic_fetch_and(&v, 0), MR_INT64_MAX);
    MR_TEST_EXPECT_EQ_INT(mr_atomic_load(&v), 0);

    /* OR with all ones sets everything */
    MR_TEST_EXPECT_EQ_INT(mr_atomic_fetch_or(&v, -1), 0);
    MR_TEST_EXPECT_EQ_INT(mr_atomic_load(&v), -1);

    /* XOR with all ones inverts every bit */
    MR_TEST_EXPECT_EQ_INT(mr_atomic_fetch_xor(&v, -1), -1);
    MR_TEST_EXPECT_EQ_INT(mr_atomic_load(&v), 0);

    /* Identity operations keep the value and return the old value */
    mr_atomic_store(&v, 0x5A5A5A5A5A5A5A5ALL);
    MR_TEST_EXPECT_EQ_INT(mr_atomic_fetch_and(&v, -1), 0x5A5A5A5A5A5A5A5ALL);
    MR_TEST_EXPECT_EQ_INT(mr_atomic_fetch_or(&v, 0), 0x5A5A5A5A5A5A5A5ALL);
    MR_TEST_EXPECT_EQ_INT(mr_atomic_fetch_xor(&v, 0), 0x5A5A5A5A5A5A5A5ALL);
    MR_TEST_EXPECT_EQ_INT(mr_atomic_load(&v), 0x5A5A5A5A5A5A5A5ALL);
}

MR_TEST(atomic, bitwise_sign_and_low_bit) {
    mr_atomic_t v;

    /* Set the sign bit on a zero value */
    mr_atomic_init(&v, 0);
    MR_TEST_EXPECT_EQ_INT(mr_atomic_fetch_or(&v, (mr_atomic_t)MR_INT64_MIN), 0);
    MR_TEST_EXPECT_EQ_INT(mr_atomic_load(&v), MR_INT64_MIN);

    /* AND keeps only the sign bit */
    MR_TEST_EXPECT_EQ_INT(mr_atomic_fetch_and(&v, (mr_atomic_t)MR_INT64_MIN),
                          MR_INT64_MIN);
    MR_TEST_EXPECT_EQ_INT(mr_atomic_load(&v), MR_INT64_MIN);

    /* XOR clears the sign bit again */
    MR_TEST_EXPECT_EQ_INT(mr_atomic_fetch_xor(&v, (mr_atomic_t)MR_INT64_MIN),
                          MR_INT64_MIN);
    MR_TEST_EXPECT_EQ_INT(mr_atomic_load(&v), 0);

    /* Lowest bit toggle round trip */
    MR_TEST_EXPECT_EQ_INT(mr_atomic_fetch_xor(&v, 1), 0);
    MR_TEST_EXPECT_EQ_INT(mr_atomic_load(&v), 1);
    MR_TEST_EXPECT_EQ_INT(mr_atomic_fetch_xor(&v, 1), 1);
    MR_TEST_EXPECT_EQ_INT(mr_atomic_load(&v), 0);
}

MR_TEST(atomic, bit32_boundary_values) {
    mr_atomic_t v;

    mr_atomic_init(&v, 0);
    mr_atomic_store(&v, 0x7FFFFFFF);
    MR_TEST_EXPECT_EQ_INT(mr_atomic_load(&v), 0x7FFFFFFF);
    MR_TEST_EXPECT_EQ_INT(mr_atomic_fetch_add(&v, 1), 0x7FFFFFFF);
    MR_TEST_EXPECT_EQ_INT(mr_atomic_load(&v), 0x80000000);

    mr_atomic_store(&v, (mr_atomic_t)0xFFFFFFFFU);
    MR_TEST_EXPECT_EQ_INT(mr_atomic_load(&v), 0xFFFFFFFF);

    mr_atomic_store(&v, (mr_atomic_t)(-2147483647 - 1));
    MR_TEST_EXPECT_EQ_INT(mr_atomic_load(&v), -2147483647 - 1);
    MR_TEST_EXPECT_EQ_INT(mr_atomic_fetch_sub(&v, 1), -2147483647 - 1);
    MR_TEST_EXPECT_EQ_INT(mr_atomic_load(&v), -2147483647LL - 2);
}

MR_TEST(atomic, load_store_sequence) {
    mr_atomic_t v;
    int i;

    mr_atomic_init(&v, 0);
    for (i = 0; i < 8; i++) {
        mr_atomic_store(&v, i * 3 - 7);
        MR_TEST_EXPECT_EQ_INT(mr_atomic_load(&v), i * 3 - 7);
    }
    MR_TEST_EXPECT_EQ_INT(mr_atomic_load(&v), 7 * 3 - 7);
}

#endif /* defined(MR_USE_ATOMIC) */
#endif /* defined(MR_USE_TEST) */
