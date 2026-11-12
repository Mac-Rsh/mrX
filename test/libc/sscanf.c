/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/test/mr_test.h>
#if defined(MR_USE_TEST)
#if defined(MR_USE_SSCANF)
#include <mrX/libc/mr_sscanf.h>
#include <mrX/libc/mr_string.h>

MR_TEST(sscanf, basic) {
    int val, ret;
    char buf[8];

    ret = mr_sscanf("42 hello", "%d %7s", &val, buf);
    MR_TEST_ASSERT_EQ_INT(ret, 2);
    MR_TEST_ASSERT_EQ_INT(val, 42);
    MR_TEST_ASSERT_EQ_INT(mr_strcmp(buf, "hello"), 0);
}

MR_TEST(sscanf, mismatch) {
    int val, ret;

    ret = mr_sscanf("abc", "%d", &val);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
}

MR_TEST(sscanf, null_empty) {
    int val;

    MR_TEST_ASSERT_EQ_INT(mr_sscanf("", "%d", &val), -1);
    MR_TEST_ASSERT_EQ_INT(mr_sscanf("123", "", &val), 0);
    MR_TEST_ASSERT_EQ_INT(mr_sscanf(MR_NULL, "%d", &val), -1);
    MR_TEST_ASSERT_EQ_INT(mr_sscanf("123", MR_NULL, &val), -1);
}

MR_TEST(sscanf, numeric_formats) {
    unsigned int u;
    int d;
    int n;
    int ret;
    char ch;

    ret = mr_sscanf("-42", "%d", &d);
    MR_TEST_ASSERT_EQ_INT(ret, 1);
    MR_TEST_ASSERT_EQ_INT(d, -42);

    ret = mr_sscanf("4294967295", "%u", &u);
    MR_TEST_ASSERT_EQ_INT(ret, 1);
    MR_TEST_ASSERT_EQ_UINT(u, 4294967295U);

    ret = mr_sscanf("1aF", "%x", &u);
    MR_TEST_ASSERT_EQ_INT(ret, 1);
    MR_TEST_ASSERT_EQ_UINT(u, 0x1AFU);

    ret = mr_sscanf("755", "%o", &u);
    MR_TEST_ASSERT_EQ_INT(ret, 1);
    MR_TEST_ASSERT_EQ_UINT(u, 0755U);

    ret = mr_sscanf("Z", "%c", &ch);
    MR_TEST_ASSERT_EQ_INT(ret, 1);
    MR_TEST_ASSERT_EQ_INT(ch, 'Z');

    ret = mr_sscanf("12abc", "%d%n", &d, &n);
    MR_TEST_ASSERT_EQ_INT(ret, 1);
    MR_TEST_ASSERT_EQ_INT(d, 12);
    MR_TEST_ASSERT_EQ_INT(n, 2);
}

MR_TEST(sscanf, width_and_whitespace) {
    int a;
    int b;
    int ret;
    char buf[8];

    ret = mr_sscanf("1234", "%2d%d", &a, &b);
    MR_TEST_ASSERT_EQ_INT(ret, 2);
    MR_TEST_ASSERT_EQ_INT(a, 12);
    MR_TEST_ASSERT_EQ_INT(b, 34);

    ret = mr_sscanf("  7\t8", "%d %d", &a, &b);
    MR_TEST_ASSERT_EQ_INT(ret, 2);
    MR_TEST_ASSERT_EQ_INT(a, 7);
    MR_TEST_ASSERT_EQ_INT(b, 8);

    ret = mr_sscanf("100%", "%d%%", &a);
    MR_TEST_ASSERT_EQ_INT(ret, 1);
    MR_TEST_ASSERT_EQ_INT(a, 100);

    ret = mr_sscanf("skip 9", "%*s %d", &a);
    MR_TEST_ASSERT_EQ_INT(ret, 1);
    MR_TEST_ASSERT_EQ_INT(a, 9);

    ret = mr_sscanf("abcXYZ", "%3s", buf);
    MR_TEST_ASSERT_EQ_INT(ret, 1);
    MR_TEST_ASSERT_EQ_INT(mr_strcmp(buf, "abc"), 0);
}

MR_TEST(sscanf, scanset_and_partial) {
    int val;
    int ret;
    char buf[8];

    ret = mr_sscanf("hello123", "%[a-z]", buf);
    MR_TEST_ASSERT_EQ_INT(ret, 1);
    MR_TEST_ASSERT_EQ_INT(mr_strcmp(buf, "hello"), 0);

    ret = mr_sscanf("42 x", "%d %d", &val, &val);
    MR_TEST_ASSERT_EQ_INT(ret, 1);
    MR_TEST_ASSERT_EQ_INT(val, 42);
}

#endif /* defined(MR_USE_SSCANF) */
#endif /* defined(MR_USE_TEST) */
