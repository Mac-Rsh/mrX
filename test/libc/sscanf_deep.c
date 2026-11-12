/**
 * @copyright (c) 2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2026-08-30        MacRsh        First version
 */

#include <mrX/test/mr_test.h>
#if defined(MR_USE_TEST)
#if defined(MR_USE_SSCANF)
#include <mrX/libc/mr_sscanf.h>
#include <mrX/libc/mr_string.h>
#include <limits.h>
#include <stdio.h>

MR_TEST(sscanf, decimal_int_limits) {
    char input[3 * sizeof(int) * CHAR_BIT + 2];
    int lower = 0;
    int upper = 0;
    int ret;

    snprintf(input, sizeof(input), "%d %d", INT_MIN, INT_MAX);
    ret = mr_sscanf(input, "%d %d", &lower, &upper);
    MR_TEST_ASSERT_EQ_INT(ret, 2);
    MR_TEST_EXPECT_EQ_INT(lower, INT_MIN);
    MR_TEST_EXPECT_EQ_INT(upper, INT_MAX);
}

MR_TEST(sscanf, decimal_unsigned_limit) {
    char input[sizeof(unsigned int) * CHAR_BIT + 1];
    unsigned int value = 0U;
    int ret;

    snprintf(input, sizeof(input), "%u", UINT_MAX);
    ret = mr_sscanf(input, "%u", &value);
    MR_TEST_ASSERT_EQ_INT(ret, 1);
    MR_TEST_EXPECT_EQ_UINT(value, UINT_MAX);
}

MR_TEST(sscanf, hhu_in_range) {
    unsigned char value = 0U;
    int ret;

    ret = mr_sscanf("255", "%hhu", &value);
    MR_TEST_ASSERT_EQ_INT(ret, 1);
    MR_TEST_ASSERT_EQ_UINT(value, 255U);
}

MR_TEST(sscanf, hd_in_range) {
    short value = 0;
    int ret;

    ret = mr_sscanf("-32768", "%hd", &value);
    MR_TEST_ASSERT_EQ_INT(ret, 1);
    MR_TEST_EXPECT_EQ_INT(value, -32768);
}

MR_TEST(sscanf, n_counts_whitespace) {
    int value = 0;
    int count = -1;
    int ret;

    ret = mr_sscanf("  42x", "%d%n", &value, &count);
    MR_TEST_ASSERT_EQ_INT(ret, 1);
    MR_TEST_ASSERT_EQ_INT(value, 42);
    MR_TEST_ASSERT_EQ_INT(count, 4);
}

MR_TEST(sscanf, n_not_written_on_mismatch) {
    int value = 0;
    int count = -1;
    int ret;

    ret = mr_sscanf("12zy", "%d x%n", &value, &count);
    MR_TEST_ASSERT_EQ_INT(ret, 1);
    MR_TEST_ASSERT_EQ_INT(value, 12);
    MR_TEST_ASSERT_EQ_INT(count, -1);
}

MR_TEST(sscanf, trailing_literal_unmatched) {
    int value = 0;
    int ret;

    ret = mr_sscanf("12", "%d x", &value);
    MR_TEST_ASSERT_EQ_INT(ret, 1);
    MR_TEST_ASSERT_EQ_INT(value, 12);
}

MR_TEST(sscanf, whitespace_only_eof) {
    int value = 0;

    MR_TEST_ASSERT_EQ_INT(mr_sscanf("   ", "%d", &value), -1);
    MR_TEST_ASSERT_EQ_INT(value, 0);
}

MR_TEST(sscanf, hex_prefix_partial) {
    unsigned int expected = 7U;
    unsigned int value = 7U;
    int expected_ret, ret;

    expected_ret = sscanf("0x", "%x", &expected);
    ret = mr_sscanf("0x", "%x", &value);
    MR_TEST_EXPECT_EQ_INT(ret, expected_ret);
    MR_TEST_EXPECT_EQ_UINT(value, expected);
}

MR_TEST(sscanf, hex_prefix_upper) {
    unsigned int value = 0U;
    int ret;

    ret = mr_sscanf("0X1F", "%x", &value);
    MR_TEST_ASSERT_EQ_INT(ret, 1);
    MR_TEST_ASSERT_EQ_UINT(value, 31U);
}

MR_TEST(sscanf, i_negative_bases) {
    int value = 0;
    int ret;

    ret = mr_sscanf("-0x10", "%i", &value);
    MR_TEST_ASSERT_EQ_INT(ret, 1);
    MR_TEST_ASSERT_EQ_INT(value, -16);

    ret = mr_sscanf("-0755", "%i", &value);
    MR_TEST_ASSERT_EQ_INT(ret, 1);
    MR_TEST_ASSERT_EQ_INT(value, -493);
}

MR_TEST(sscanf, u_explicit_plus) {
    unsigned int value = 0U;
    int ret;

    ret = mr_sscanf("+5", "%u", &value);
    MR_TEST_ASSERT_EQ_INT(ret, 1);
    MR_TEST_ASSERT_EQ_UINT(value, 5U);
}

MR_TEST(sscanf, u_negative_value) {
    unsigned int value = 0U;
    int ret;

    ret = mr_sscanf("-1", "%u", &value);
    MR_TEST_ASSERT_EQ_INT(ret, 1);
    MR_TEST_ASSERT_EQ_UINT(value, 4294967295U);
}

MR_TEST(sscanf, double_sign_mismatch) {
    int value = -999;
    int ret;

    ret = mr_sscanf("++5", "%d", &value);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(value, -999);
}

#endif /* defined(MR_USE_SSCANF) */
#endif /* defined(MR_USE_TEST) */
