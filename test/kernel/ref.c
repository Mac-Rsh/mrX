/**
 * @copyright (c) 2024-2025, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mr-X/test/mr_test.h>
#if defined(MR_USE_TEST)
#include <mr-X/kernel/mr_ref.h>

static void stub_release(mr_ref_t *ref) {
    *ref = 0xdeadbeef;
}

/* -------------------------------------------------------------------------- */
/* Test: static init                                                          */
static void test_ref_static_init(void) {
    static mr_ref_t r = MR_REF_INIT();

    MR_TEST_ASSERT_EQ_INT(MR_REF_IS_INITED(r), MR_TRUE);
    MR_TEST_ASSERT_EQ_INT(MR_REF_COUNT(r), 1);
}
MR_TEST_EXPORT(ref, static_init, test_ref_static_init);

/* -------------------------------------------------------------------------- */
/* Test: init/inited/count                                                    */
static void test_ref_init(void) {
    mr_ref_t r;

    mr_ref_init(&r);
    MR_TEST_ASSERT_EQ_INT(MR_REF_IS_INITED(r), MR_TRUE);
    MR_TEST_ASSERT_EQ_INT(MR_REF_COUNT(r), 1);
}
MR_TEST_EXPORT(ref, init, test_ref_init);

/* -------------------------------------------------------------------------- */
/* Test: get increments count                                                 */
static void test_ref_get(void) {
    mr_ref_t r;

    mr_ref_init(&r);
    MR_TEST_ASSERT_EQ_INT(mr_ref_get(&r), MR_TRUE);
    MR_TEST_ASSERT_EQ_INT(MR_REF_COUNT(r), 2);
    MR_TEST_ASSERT_EQ_INT(mr_ref_get(&r), MR_TRUE);
    MR_TEST_ASSERT_EQ_INT(MR_REF_COUNT(r), 3);
}
MR_TEST_EXPORT(ref, get, test_ref_get);

/* -------------------------------------------------------------------------- */
/* Test: put decrements and release                                           */
static void test_ref_put(void) {
    mr_ref_t r;

    mr_ref_init(&r);
    MR_TEST_ASSERT_EQ_INT(mr_ref_put(&r, stub_release), MR_TRUE);
    MR_TEST_ASSERT_EQ_INT(r & 0xffffffff, 0xdeadbeef);
    MR_TEST_ASSERT_EQ_INT(mr_ref_put(&r, stub_release), MR_FALSE);
    MR_TEST_ASSERT_EQ_INT(r & 0xffffffff, 0xdeadbeef - 1);
}
MR_TEST_EXPORT(ref, put, test_ref_put);

/* -------------------------------------------------------------------------- */
/* Test: get after release fails                                              */
static void test_ref_get_fail(void) {
    mr_ref_t r;

    mr_ref_init(&r);
    mr_ref_put(&r, stub_release);
    MR_TEST_ASSERT_EQ_INT(mr_ref_get(&r), MR_FALSE);
}
MR_TEST_EXPORT(ref, get_fail, test_ref_get_fail);
#endif /* defined(MR_USE_TEST) */
