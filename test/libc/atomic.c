/**
 * @copyright (c) 2024-2025, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/test/mr_test.h>
#if defined(MR_USE_TEST)
#include <mrX/libc/mr_atomic.h>

/* -------------------------------------------------------------------------- */
/* Test: static init                                                          */
static void test_atomic_static_init(void) {
    mr_atomic_t v = MR_ATOMIC_INIT(12345);

    MR_TEST_ASSERT_EQ_INT(mr_atomic_load(&v), 12345);
}
MR_TEST_EXPORT(atomic, static_init, test_atomic_static_init);

/* -------------------------------------------------------------------------- */
/* Test: load/store                                                           */
static void test_atomic_load_store(void) {
    mr_atomic_t v;

    mr_atomic_init(&v, 0);
    mr_atomic_store(&v, 42);
    MR_TEST_ASSERT_EQ_INT(mr_atomic_load(&v), 42);
}
MR_TEST_EXPORT(atomic, load_store, test_atomic_load_store);

/* -------------------------------------------------------------------------- */
/* Test: exchange                                                             */
static void test_atomic_exchange(void) {
    mr_atomic_t v, old;

    mr_atomic_init(&v, 5);
    old = mr_atomic_exchange(&v, 7);
    MR_TEST_ASSERT_EQ_INT(old, 5);
    MR_TEST_ASSERT_EQ_INT(mr_atomic_load(&v), 7);
}
MR_TEST_EXPORT(atomic, exchange, test_atomic_exchange);

/* -------------------------------------------------------------------------- */
/* Test: compare-exchange success/fail                                        */
static void test_atomic_compare_exchange(void) {
    mr_atomic_t v, expect;

    mr_atomic_init(&v, 3);
    mr_atomic_store(&expect, 3);
    MR_TEST_ASSERT_EQ_INT(mr_atomic_compare_exchange(&v, &expect, 9), MR_TRUE);
    MR_TEST_ASSERT_EQ_INT(v, 9);

    expect = 3;
    MR_TEST_ASSERT_EQ_INT(mr_atomic_compare_exchange(&v, &expect, 1), MR_FALSE);
    MR_TEST_ASSERT_EQ_INT(expect, 9);
}
MR_TEST_EXPORT(atomic, compare_exchange, test_atomic_compare_exchange);

/* -------------------------------------------------------------------------- */
/* Test: fetch-add                                                            */
static void test_atomic_fetch_add(void) {
    mr_atomic_t v;

    mr_atomic_init(&v, 10);
    MR_TEST_ASSERT_EQ_INT(mr_atomic_fetch_add(&v, 5), 10);
    MR_TEST_ASSERT_EQ_INT(v, 15);
}
MR_TEST_EXPORT(atomic, fetch_add, test_atomic_fetch_add);

/* -------------------------------------------------------------------------- */
/* Test: fetch-sub                                                            */
static void test_atomic_fetch_sub(void) {
    mr_atomic_t v;

    mr_atomic_init(&v, 20);
    MR_TEST_ASSERT_EQ_INT(mr_atomic_fetch_sub(&v, 8), 20);
    MR_TEST_ASSERT_EQ_INT(v, 12);
}
MR_TEST_EXPORT(atomic, fetch_sub, test_atomic_fetch_sub);

/* -------------------------------------------------------------------------- */
/* Test: bitwise fetch-and/or/xor                                             */
static void test_atomic_bitwise(void) {
    mr_atomic_t v;

    mr_atomic_init(&v, 0x0F);
    MR_TEST_ASSERT_EQ_INT(mr_atomic_fetch_and(&v, 0x07), 0x0F);
    MR_TEST_ASSERT_EQ_INT(v, 0x07);

    MR_TEST_ASSERT_EQ_INT(mr_atomic_fetch_or(&v, 0x08), 0x07);
    MR_TEST_ASSERT_EQ_INT(v, 0x0F);

    MR_TEST_ASSERT_EQ_INT(mr_atomic_fetch_xor(&v, 0x0F), 0x0F);
    MR_TEST_ASSERT_EQ_INT(v, 0x00);
}
MR_TEST_EXPORT(atomic, bitwise, test_atomic_bitwise);
#endif /* defined(MR_USE_TEST) */
