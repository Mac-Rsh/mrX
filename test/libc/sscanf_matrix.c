/**
 * @copyright (c) 2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2026-08-29        MacRsh        First version
 */

#include <mrX/test/mr_test.h>
#if defined(MR_USE_TEST) && defined(MR_USE_SSCANF)
#include <mrX/libc/mr_sscanf.h>
#include <mrX/libc/mr_string.h>

/* Keep parsing inputs independently selectable for focused regressions */
#define SSCANF_INT_CASE(_name, _input, _format, _ret, _expected)               \
    MR_TEST(sscanf_matrix, _name) {                                            \
        int value = -999;                                                      \
        int ret;                                                               \
                                                                               \
        ret = mr_sscanf(_input, _format, &value);                              \
        MR_TEST_ASSERT_EQ_INT(ret, _ret);                                      \
        MR_TEST_ASSERT_EQ_INT(value, _expected);                               \
    }

#define SSCANF_UINT_CASE(_name, _input, _format, _ret, _expected)              \
    MR_TEST(sscanf_matrix, _name) {                                            \
        unsigned int value = 0U;                                               \
        int ret;                                                               \
                                                                               \
        ret = mr_sscanf(_input, _format, &value);                              \
        MR_TEST_ASSERT_EQ_INT(ret, _ret);                                      \
        MR_TEST_ASSERT_EQ_UINT(value, _expected);                              \
    }

#define SSCANF_STRING_CASE(_name, _input, _format, _ret, _expected)            \
    MR_TEST(sscanf_matrix, _name) {                                            \
        char value[16];                                                        \
        int ret;                                                               \
                                                                               \
        mr_memset(value, 0, sizeof(value));                                    \
        ret = mr_sscanf(_input, _format, value);                               \
        MR_TEST_ASSERT_EQ_INT(ret, _ret);                                      \
        MR_TEST_ASSERT_EQ_INT(mr_strcmp(value, _expected), 0);                 \
    }

#define SSCANF_CHAR_CASE(_name, _input, _format, _expected)                    \
    MR_TEST(sscanf_matrix, _name) {                                            \
        char value;                                                            \
        int ret;                                                               \
                                                                               \
        value = '\0';                                                          \
        ret = mr_sscanf(_input, _format, &value);                              \
        MR_TEST_ASSERT_EQ_INT(ret, 1);                                         \
        MR_TEST_ASSERT_EQ_INT(value, _expected);                               \
    }

/* Decimal parsing reaches sign, whitespace, width, and mismatch paths */
SSCANF_INT_CASE(decimal_zero, "0", "%d", 1, 0)
SSCANF_INT_CASE(decimal_positive, "73", "%d", 1, 73)
SSCANF_INT_CASE(decimal_negative, "-73", "%d", 1, -73)
SSCANF_INT_CASE(decimal_positive_sign, "+73", "%d", 1, 73)
SSCANF_INT_CASE(decimal_leading_space, "  \t73", "%d", 1, 73)
SSCANF_INT_CASE(decimal_width, "1234", "%2d", 1, 12)
SSCANF_INT_CASE(decimal_suffix, "12x", "%d", 1, 12)
SSCANF_INT_CASE(decimal_mismatch, "x", "%d", 0, -999)
SSCANF_INT_CASE(integer_auto_decimal, "73", "%i", 1, 73)
SSCANF_INT_CASE(integer_auto_hex, "0x20", "%i", 1, 32)
SSCANF_INT_CASE(integer_auto_octal, "0755", "%i", 1, 0755)

/* Unsigned base conversion covers standard prefixes and field widths */
SSCANF_UINT_CASE(unsigned_zero, "0", "%u", 1, 0U)
SSCANF_UINT_CASE(unsigned_max, "4294967295", "%u", 1, 4294967295U)
SSCANF_UINT_CASE(unsigned_width, "1234", "%3u", 1, 123U)
SSCANF_UINT_CASE(hex_lower, "1af", "%x", 1, 0x1afU)
SSCANF_UINT_CASE(hex_prefix, "0x1AF", "%x", 1, 0x1afU)
SSCANF_UINT_CASE(octal, "755", "%o", 1, 0755U)

/* String and scanset conversions differ in whitespace and terminator rules */
SSCANF_STRING_CASE(string_plain, "hello", "%15s", 1, "hello")
SSCANF_STRING_CASE(string_width, "abcdef", "%3s", 1, "abc")
SSCANF_STRING_CASE(string_whitespace, "  hello world", "%15s", 1, "hello")
SSCANF_STRING_CASE(scanset_lower, "hello123", "%[a-z]", 1, "hello")
SSCANF_STRING_CASE(scanset_digits, "123abc", "%[0-9]", 1, "123")

/* Character conversion does not skip whitespace */
SSCANF_CHAR_CASE(char_plain, "Z", "%c", 'Z')
SSCANF_CHAR_CASE(char_whitespace, " Z", "%c", ' ')

MR_TEST(sscanf_matrix, chars_width) {
    char value[4];
    int ret;

    mr_memset(value, 0, sizeof(value));
    ret = mr_sscanf("abcd", "%3c", value);
    MR_TEST_ASSERT_EQ_INT(ret, 1);
    MR_TEST_ASSERT_EQ_INT(mr_memcmp(value, "abc", 3), 0);
}

MR_TEST(sscanf_matrix, literal_percent) {
    int value;
    int ret;

    ret = mr_sscanf("100%", "%d%%", &value);
    MR_TEST_ASSERT_EQ_INT(ret, 1);
    MR_TEST_ASSERT_EQ_INT(value, 100);
}

MR_TEST(sscanf_matrix, literal_mismatch) {
    MR_TEST_ASSERT_EQ_INT(mr_sscanf("x", "y"), 0);
}

MR_TEST(sscanf_matrix, assignment_suppression) {
    int value;
    int ret;

    ret = mr_sscanf("skip 9", "%*s %d", &value);
    MR_TEST_ASSERT_EQ_INT(ret, 1);
    MR_TEST_ASSERT_EQ_INT(value, 9);
}

MR_TEST(sscanf_matrix, assignment_count) {
    int value;
    int count;
    int ret;

    ret = mr_sscanf("12abc", "%d%n", &value, &count);
    MR_TEST_ASSERT_EQ_INT(ret, 1);
    MR_TEST_ASSERT_EQ_INT(value, 12);
    MR_TEST_ASSERT_EQ_INT(count, 2);
}

MR_TEST(sscanf_matrix, partial_assignment) {
    int first;
    int second;
    int ret;

    ret = mr_sscanf("12 x", "%d %d", &first, &second);
    MR_TEST_ASSERT_EQ_INT(ret, 1);
    MR_TEST_ASSERT_EQ_INT(first, 12);
}

MR_TEST(sscanf_matrix, inverted_scanset) {
    char value[8] = {'X', '\0'};
    int ret;

    ret = mr_sscanf("abc123", "%7[^0-9]", value);
    MR_TEST_ASSERT_EQ_INT(ret, 1);
    MR_TEST_ASSERT_EQ_INT(mr_strcmp(value, "abc"), 0);

    ret = mr_sscanf("123", "%7[^0-9]", value);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(mr_strcmp(value, "abc"), 0);
}

MR_TEST(sscanf_matrix, empty_input) {
    int value;

    MR_TEST_ASSERT_EQ_INT(mr_sscanf("", "%d", &value), -1);
}

#undef SSCANF_CHAR_CASE
#undef SSCANF_STRING_CASE
#undef SSCANF_UINT_CASE
#undef SSCANF_INT_CASE

#endif /* defined(MR_USE_TEST) && defined(MR_USE_SSCANF) */
