/**
 * @copyright (c) 2024-2025, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <test/mr_test.h>
#if defined(MR_USE_TEST)
#include <libc/mr_sscanf.h>
#include <libc/mr_string.h>

/* -------------------------------------------------------------------------- */
/* Test: basic integer and string extraction                                  */
static void test_sscanf_basic(void) {
    int val, ret;
    char buf[8];

    ret = mr_sscanf("42 hello", "%d %7s", &val, buf);
    MR_TEST_ASSERT_EQ_INT(ret, 2);
    MR_TEST_ASSERT_EQ_INT(val, 42);
    MR_TEST_ASSERT_EQ_INT(mr_strcmp(buf, "hello"), 0);
}
MR_TEST_EXPORT(sscanf, basic, test_sscanf_basic);

/* -------------------------------------------------------------------------- */
/* Test: format mismatch                                                      */
static void test_sscanf_mismatch(void) {
    int val, ret;

    ret = mr_sscanf("abc", "%d", &val);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
}
MR_TEST_EXPORT(sscanf, mismatch, test_sscanf_mismatch);

/* -------------------------------------------------------------------------- */
/* Test: null / empty input                                                   */
static void test_sscanf_null_empty(void) {
    int val;

    MR_TEST_ASSERT_EQ_INT(mr_sscanf("", "%d", &val), -1);
    MR_TEST_ASSERT_EQ_INT(mr_sscanf("123", "", &val), 0);
}
MR_TEST_EXPORT(sscanf, null_empty, test_sscanf_null_empty);
#endif /* defined(MR_USE_TEST) */
