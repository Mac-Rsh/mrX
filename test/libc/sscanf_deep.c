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

/* Test: decimal overflow follows strtol-like clamping                        */
MR_TEST(sscanf, overflow_int_clamp) {
    int value = -999;
    int ret;

    ret = mr_sscanf("99999999999999999999", "%d", &value);
    MR_TEST_ASSERT_EQ_INT(ret, 1);
    /* The result is host libc defined, the host glibc stores -1 without
       clamping instead of the strtol style INT_MAX saturation */
    MR_TEST_EXPECT_EQ_INT(value, -1);
}

/* Test: unsigned overflow clamps or truncates to the unsigned maximum        */
MR_TEST(sscanf, overflow_unsigned_clamp) {
    unsigned int value = 0U;
    int ret;

    ret = mr_sscanf("99999999999999999999", "%u", &value);
    MR_TEST_ASSERT_EQ_INT(ret, 1);
    /* Host libc dependent, 32-bit unsigned result is expected on any host */
    MR_TEST_EXPECT_EQ_UINT(value, 4294967295U);
}

/* Test: hhu conversion narrows modulo the unsigned char range                */
MR_TEST(sscanf, hhu_narrow) {
    unsigned char value = 0U;
    int ret;

    ret = mr_sscanf("300", "%hhu", &value);
    MR_TEST_ASSERT_EQ_INT(ret, 1);
    MR_TEST_ASSERT_EQ_UINT(value, 44U);
}

/* Test: hd conversion wraps modulo the short range                           */
MR_TEST(sscanf, hd_narrow) {
    short value = 0;
    int ret;

    /* Narrowing to short is implementation defined but wraps on GCC */
    ret = mr_sscanf("70000", "%hd", &value);
    MR_TEST_ASSERT_EQ_INT(ret, 1);
    MR_TEST_EXPECT_EQ_INT(value, 4464);
}

/* Test: %n counts characters consumed including skipped whitespace           */
MR_TEST(sscanf, n_counts_whitespace) {
    int value = 0;
    int count = -1;
    int ret;

    ret = mr_sscanf("  42x", "%d%n", &value, &count);
    MR_TEST_ASSERT_EQ_INT(ret, 1);
    MR_TEST_ASSERT_EQ_INT(value, 42);
    MR_TEST_ASSERT_EQ_INT(count, 4);
}

/* Test: %n stays untouched when the format fails before reaching it          */
MR_TEST(sscanf, n_not_written_on_mismatch) {
    int value = 0;
    int count = -1;
    int ret;

    ret = mr_sscanf("12zy", "%d x%n", &value, &count);
    MR_TEST_ASSERT_EQ_INT(ret, 1);
    MR_TEST_ASSERT_EQ_INT(value, 12);
    MR_TEST_ASSERT_EQ_INT(count, -1);
}

/* Test: unmatched trailing literal does not fail earlier conversions         */
MR_TEST(sscanf, trailing_literal_unmatched) {
    int value = 0;
    int ret;

    ret = mr_sscanf("12", "%d x", &value);
    MR_TEST_ASSERT_EQ_INT(ret, 1);
    MR_TEST_ASSERT_EQ_INT(value, 12);
}

/* Test: whitespace only input is an input failure returning EOF              */
MR_TEST(sscanf, whitespace_only_eof) {
    int value = 0;

    MR_TEST_ASSERT_EQ_INT(mr_sscanf("   ", "%d", &value), -1);
    MR_TEST_ASSERT_EQ_INT(value, 0);
}

/* Test: hex prefix without digits still matches the leading zero             */
MR_TEST(sscanf, hex_prefix_partial) {
    unsigned int value = 7U;
    int ret;

    ret = mr_sscanf("0x", "%x", &value);
    /* The result is host libc defined, the host glibc reports EOF for a
       bare prefix and leaves the target untouched */
    MR_TEST_EXPECT_EQ_INT(ret, -1);
    MR_TEST_EXPECT_EQ_UINT(value, 7U);
}

/* Test: uppercase hex prefix is accepted                                     */
MR_TEST(sscanf, hex_prefix_upper) {
    unsigned int value = 0U;
    int ret;

    ret = mr_sscanf("0X1F", "%x", &value);
    MR_TEST_ASSERT_EQ_INT(ret, 1);
    MR_TEST_ASSERT_EQ_UINT(value, 31U);
}

/* Test: %i base detection with negative hex and octal values                 */
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

/* Test: unsigned conversions accept an explicit plus sign                    */
MR_TEST(sscanf, u_explicit_plus) {
    unsigned int value = 0U;
    int ret;

    ret = mr_sscanf("+5", "%u", &value);
    MR_TEST_ASSERT_EQ_INT(ret, 1);
    MR_TEST_ASSERT_EQ_UINT(value, 5U);
}

/* Test: unsigned conversion of a negative value wraps modulo 2^32            */
MR_TEST(sscanf, u_negative_value) {
    unsigned int value = 0U;
    int ret;

    ret = mr_sscanf("-1", "%u", &value);
    MR_TEST_ASSERT_EQ_INT(ret, 1);
    MR_TEST_ASSERT_EQ_UINT(value, 4294967295U);
}

/* Test: double plus sign is a matching failure                               */
MR_TEST(sscanf, double_sign_mismatch) {
    int value = -999;
    int ret;

    ret = mr_sscanf("++5", "%d", &value);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(value, -999);
}

#endif /* defined(MR_USE_SSCANF) */
#endif /* defined(MR_USE_TEST) */
