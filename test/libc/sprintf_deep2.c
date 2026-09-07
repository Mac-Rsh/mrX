/**
 * @copyright (c) 2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2026-08-30        MacRsh        First version
 */

#include <mrX/test/mr_test.h>
#if defined(MR_USE_TEST)
#if defined(MR_USE_SPRINTF)
#include <mrX/libc/mr_sprintf.h>
#include <mrX/libc/mr_string.h>
#include <limits.h>

/* Test: octal alternate form with precision                                  */
MR_TEST(sprintf, prefix_octal_precision) {
    char buf[32];
    int ret;

    /* C99 7.19.6.1: for 'o' the # flag only increases precision to force a
     * leading zero, it must not add one beyond the requested precision
     */
    ret = mr_sprintf(buf, "%#.3o", 8U);
    MR_TEST_EXPECT_EQ_INT(ret, 3);
    MR_TEST_EXPECT_EQ_INT(mr_strcmp(buf, "010"), 0);
}

/* Test: hex alternate form with zero pad and precision combined              */
MR_TEST(sprintf, prefix_hex_zero_pad_precision) {
    char buf[32];
    int ret;

    ret = mr_sprintf(buf, "%#08.5x", 0x1aU);
    MR_TEST_ASSERT_EQ_INT(ret, 8);
    MR_TEST_ASSERT_EQ_INT(mr_strcmp(buf, "0x00001a"), 0);
}

/* Test: hex alternate form with left justify and width                       */
MR_TEST(sprintf, prefix_hex_left_width) {
    char buf[32];
    int ret;

    ret = mr_sprintf(buf, "%-#8x", 0x1aU);
    MR_TEST_ASSERT_EQ_INT(ret, 8);
    MR_TEST_ASSERT_EQ_INT(mr_strcmp(buf, "0x1a    "), 0);
}

/* Test: alternate prefixes follow ordinary left space padding               */
MR_TEST(sprintf, prefix_space_width) {
    char buf[32];
    int ret;

    ret = mr_sprintf(buf, "%#6x|%#6o|%#7b", 0x12U, 8U, 3U);
    MR_TEST_ASSERT_EQ_INT(ret, 21);
    MR_TEST_ASSERT_EQ_INT(mr_strcmp(buf, "  0x12|   010|   0b11"), 0);
}

/* Test: octal alternate form with zero pad and width                         */
MR_TEST(sprintf, prefix_octal_zero_pad) {
    char buf[32];
    int ret;

    ret = mr_sprintf(buf, "%#08o", 0755U);
    MR_TEST_ASSERT_EQ_INT(ret, 8);
    MR_TEST_ASSERT_EQ_INT(mr_strcmp(buf, "00000755"), 0);
}

/* Test: plus sign with precision                                             */
MR_TEST(sprintf, plus_precision) {
    char buf[32];
    int ret;

    ret = mr_sprintf(buf, "%+.4d", 42);
    MR_TEST_ASSERT_EQ_INT(ret, 5);
    MR_TEST_ASSERT_EQ_INT(mr_strcmp(buf, "+0042"), 0);
}

/* Test: left justify with zero flag and precision                            */
MR_TEST(sprintf, left_zero_precision) {
    char buf[32];
    int ret;

    ret = mr_sprintf(buf, "%-08.3d", 12);
    MR_TEST_ASSERT_EQ_INT(ret, 8);
    MR_TEST_ASSERT_EQ_INT(mr_strcmp(buf, "012     "), 0);
}

/* Test: precision larger than width wins                                     */
MR_TEST(sprintf, precision_exceeds_width) {
    char buf[32];
    int ret;

    ret = mr_sprintf(buf, "%5.10d", 42);
    MR_TEST_ASSERT_EQ_INT(ret, 10);
    MR_TEST_ASSERT_EQ_INT(mr_strcmp(buf, "0000000042"), 0);
}

/* Test: dynamic width of zero does not pad                                   */
MR_TEST(sprintf, star_width_zero) {
    char buf[32];
    int ret;

    ret = mr_sprintf(buf, "%*d", 0, 42);
    MR_TEST_ASSERT_EQ_INT(ret, 2);
    MR_TEST_ASSERT_EQ_INT(mr_strcmp(buf, "42"), 0);
}

/* Test: string precision far beyond the string length terminates             */
MR_TEST(sprintf, string_huge_precision) {
    char buf[32];
    int ret;

    ret = mr_sprintf(buf, "%.999999s", "hi");
    MR_TEST_ASSERT_EQ_INT(ret, 2);
    MR_TEST_ASSERT_EQ_INT(mr_strcmp(buf, "hi"), 0);
}

/* Test: string precision beyond length with width padding                    */
MR_TEST(sprintf, string_precision_beyond_len) {
    char buf[32];
    int ret;

    ret = mr_sprintf(buf, "%10.6s", "hi");
    MR_TEST_ASSERT_EQ_INT(ret, 10);
    MR_TEST_ASSERT_EQ_INT(mr_strcmp(buf, "        hi"), 0);
}

/* Test: character conversion ignores precision                               */
MR_TEST(sprintf, char_ignores_precision) {
    char buf[32];
    int ret;

    ret = mr_sprintf(buf, "%5.3c", 'A');
    MR_TEST_ASSERT_EQ_INT(ret, 5);
    MR_TEST_ASSERT_EQ_INT(mr_strcmp(buf, "    A"), 0);
}

/* Test: hh modifier narrowing uses modulo wrap for out of range values       */
MR_TEST(sprintf, hh_narrow_modulo) {
    char buf[32];
    int ret;

    /* Narrowing to signed char is implementation defined but wraps on GCC */
    ret = mr_sprintf(buf, "%hhd|%hhu", 300, 300);
    MR_TEST_EXPECT_EQ_INT(ret, 5);
    MR_TEST_EXPECT_EQ_INT(mr_strcmp(buf, "44|44"), 0);
}

/* Test: h modifier narrowing uses modulo wrap for out of range values        */
MR_TEST(sprintf, h_narrow_modulo) {
    char buf[32];
    int ret;

    ret = mr_sprintf(buf, "%hd|%hu", 32768, 65536);
    MR_TEST_EXPECT_EQ_INT(ret, 8);
    MR_TEST_EXPECT_EQ_INT(mr_strcmp(buf, "-32768|0"), 0);
}

/* Test: 64-bit unsigned maximum across hex and octal bases                   */
MR_TEST(sprintf, ll_unsigned_max_bases) {
    char buf[64];
    int ret;

    ret = mr_sprintf(buf, "%llx|%llo", ULLONG_MAX, ULLONG_MAX);
    MR_TEST_ASSERT_EQ_INT(ret, 39);
    MR_TEST_ASSERT_EQ_INT(
        mr_strcmp(buf, "ffffffffffffffff|1777777777777777777777"), 0);
}

/* Test: unknown conversions echo both characters and keep arguments aligned  */
MR_TEST(sprintf, unknown_conv_echo) {
    char buf[32];
    int ret;

    ret = mr_sprintf(buf, "a%qb", 1);
    MR_TEST_ASSERT_EQ_INT(ret, 4);
    MR_TEST_ASSERT_EQ_INT(mr_strcmp(buf, "a%qb"), 0);

    /* Trailing percent with no conversion echoes once and terminates */
    ret = mr_sprintf(buf, "abc%");
    MR_TEST_ASSERT_EQ_INT(ret, 4);
    MR_TEST_ASSERT_EQ_INT(mr_strcmp(buf, "abc%"), 0);
}

/* Test: float conversion consumes its argument and echoes the specifier      */
MR_TEST(sprintf, float_conv_consumes_arg) {
    char buf[32];
    int ret;

    ret = mr_sprintf(buf, "x%f=%d", 1.5, 42);
    /* The echoed specifier is six characters in total */
    MR_TEST_ASSERT_EQ_INT(ret, 6);
    MR_TEST_ASSERT_EQ_INT(mr_strcmp(buf, "x%f=42"), 0);
}

/* Test: sprintf output is not limited by any internal buffer cap             */
MR_TEST(sprintf, sprintf_no_buffer_cap) {
    char buf[128];
    int ret;

    ret = mr_sprintf(buf, "%-100d", 7);
    MR_TEST_ASSERT_EQ_INT(ret, 100);
    MR_TEST_ASSERT_EQ_INT(buf[0], '7');
    MR_TEST_ASSERT_EQ_INT(buf[99], ' ');
    MR_TEST_ASSERT_EQ_INT(buf[100], '\0');
}

/* Test: snprintf with NULL buffer and nonzero size returns needed length     */
MR_TEST(sprintf, snprintf_null_buf_nonzero_size) {
    int ret;

    ret = mr_snprintf(MR_NULL, 16, "%d", 12345);
    MR_TEST_ASSERT_EQ_INT(ret, 5);
}

/* Test: snprintf truncation returns the needed length not the clipped length */
MR_TEST(sprintf, snprintf_truncation_returns_needed) {
    char buf[6];
    int ret;

    ret = mr_snprintf(buf, sizeof(buf), "%d-%d", 123, 456);
    MR_TEST_ASSERT_EQ_INT(ret, 7);
    MR_TEST_ASSERT_EQ_INT(mr_strcmp(buf, "123-4"), 0);
}

/* Test: pointer with explicit width keeps the requested field width          */
MR_TEST(sprintf, pointer_explicit_width) {
    const void *ptr = (void *)0x1234;
    char buf[32];
    int ret;

    ret = mr_sprintf(buf, "%12p", ptr);
    MR_TEST_ASSERT_EQ_INT(ret, 12);
}

#endif /* defined(MR_USE_SPRINTF) */
#endif /* defined(MR_USE_TEST) */
