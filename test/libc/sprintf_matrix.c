/**
 * @copyright (c) 2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2026-08-29        MacRsh        First version
 */

#include <mrX/test/mr_test.h>
#if defined(MR_USE_TEST) && defined(MR_USE_SPRINTF)
#include <mrX/libc/mr_sprintf.h>
#include <mrX/libc/mr_string.h>
#include <limits.h>

/* Each table entry is deliberately a separately selectable regression test */
#define SPRINTF_CASE0(_name, _format, _expected)                               \
    MR_TEST(sprintf_matrix, _name) {                                           \
        char buffer[96];                                                       \
        int ret;                                                               \
                                                                               \
        ret = mr_sprintf(buffer, _format);                                     \
        MR_TEST_ASSERT_EQ_INT(ret, (mr_int64_t)mr_strlen(_expected));          \
        MR_TEST_ASSERT_EQ_INT(mr_strcmp(buffer, _expected), 0);                \
    }

#define SPRINTF_CASE1(_name, _format, _value, _expected)                       \
    MR_TEST(sprintf_matrix, _name) {                                           \
        char buffer[96];                                                       \
        int ret;                                                               \
                                                                               \
        ret = mr_sprintf(buffer, _format, _value);                             \
        MR_TEST_ASSERT_EQ_INT(ret, (mr_int64_t)mr_strlen(_expected));          \
        MR_TEST_ASSERT_EQ_INT(mr_strcmp(buffer, _expected), 0);                \
    }

#define SPRINTF_CASE2(_name, _format, _first, _second, _expected)              \
    MR_TEST(sprintf_matrix, _name) {                                           \
        char buffer[96];                                                       \
        int ret;                                                               \
                                                                               \
        ret = mr_sprintf(buffer, _format, _first, _second);                    \
        MR_TEST_ASSERT_EQ_INT(ret, (mr_int64_t)mr_strlen(_expected));          \
        MR_TEST_ASSERT_EQ_INT(mr_strcmp(buffer, _expected), 0);                \
    }

#define SPRINTF_CASE3(_name, _format, _first, _second, _third, _expected)      \
    MR_TEST(sprintf_matrix, _name) {                                           \
        char buffer[96];                                                       \
        int ret;                                                               \
                                                                               \
        ret = mr_sprintf(buffer, _format, _first, _second, _third);            \
        MR_TEST_ASSERT_EQ_INT(ret, (mr_int64_t)mr_strlen(_expected));          \
        MR_TEST_ASSERT_EQ_INT(mr_strcmp(buffer, _expected), 0);                \
    }

/* Decimal conversion and sign handling */
SPRINTF_CASE1(decimal_zero, "%d", 0, "0")
SPRINTF_CASE1(decimal_one, "%d", 1, "1")
SPRINTF_CASE1(decimal_negative_one, "%d", -1, "-1")
SPRINTF_CASE1(decimal_int_max, "%d", INT_MAX, "2147483647")
SPRINTF_CASE1(decimal_int_min, "%d", INT_MIN, "-2147483648")
SPRINTF_CASE1(integer_zero, "%i", 0, "0")
SPRINTF_CASE1(integer_positive, "%i", 73, "73")
SPRINTF_CASE1(integer_negative, "%i", -73, "-73")
SPRINTF_CASE1(unsigned_zero, "%u", 0U, "0")
SPRINTF_CASE1(unsigned_one, "%u", 1U, "1")
SPRINTF_CASE1(unsigned_max, "%u", UINT_MAX, "4294967295")
SPRINTF_CASE1(unsigned_mid, "%u", 4000000000U, "4000000000")

/* Alternate bases exercise every digit table and prefix branch */
SPRINTF_CASE1(hex_zero, "%x", 0U, "0")
SPRINTF_CASE1(hex_lower_digits, "%x", 0xabcdefU, "abcdef")
SPRINTF_CASE1(hex_lower_mixed, "%x", 0x10afU, "10af")
SPRINTF_CASE1(hex_lower_max, "%x", UINT_MAX, "ffffffff")
SPRINTF_CASE1(hex_upper_digits, "%X", 0xabcdefU, "ABCDEF")
SPRINTF_CASE1(hex_upper_mixed, "%X", 0x10afU, "10AF")
SPRINTF_CASE1(hex_upper_max, "%X", UINT_MAX, "FFFFFFFF")
SPRINTF_CASE1(octal_zero, "%o", 0U, "0")
SPRINTF_CASE1(octal_one, "%o", 1U, "1")
SPRINTF_CASE1(octal_mixed, "%o", 01234567U, "1234567")
SPRINTF_CASE1(octal_max, "%o", UINT_MAX, "37777777777")
SPRINTF_CASE1(binary_zero, "%b", 0U, "0")
SPRINTF_CASE1(binary_one, "%b", 1U, "1")
SPRINTF_CASE1(binary_mixed, "%b", 0x25U, "100101")
SPRINTF_CASE1(binary_high_bit, "%b", 0x80000000U,
              "10000000000000000000000000000000")

/* Width, padding, and sign flags use mutually exclusive formatter paths */
SPRINTF_CASE1(width_decimal, "%5d", 12, "   12")
SPRINTF_CASE1(width_zero_decimal, "%05d", 12, "00012")
SPRINTF_CASE1(width_left_decimal, "%-5d", 12, "12   ")
SPRINTF_CASE1(width_negative_decimal, "%5d", -12, "  -12")
SPRINTF_CASE1(width_zero_negative_decimal, "%05d", -12, "-0012")
SPRINTF_CASE1(width_left_negative_decimal, "%-5d", -12, "-12  ")
SPRINTF_CASE1(sign_plus_positive, "%+d", 12, "+12")
SPRINTF_CASE1(sign_plus_negative, "%+d", -12, "-12")
SPRINTF_CASE1(sign_space_positive, "% d", 12, " 12")
SPRINTF_CASE1(sign_space_negative, "% d", -12, "-12")
SPRINTF_CASE1(prefix_hex_lower, "%#x", 0x12U, "0x12")
SPRINTF_CASE1(prefix_hex_upper, "%#X", 0x12U, "0X12")
SPRINTF_CASE1(prefix_octal, "%#o", 012U, "012")
SPRINTF_CASE1(prefix_binary, "%#b", 5U, "0b101")
SPRINTF_CASE1(prefix_width_hex, "%#08x", 0xffU, "0x0000ff")
SPRINTF_CASE1(prefix_left_binary, "%-#8b", 3U, "0b11    ")

/* Alternate form does not add a redundant base marker for a zero value */
SPRINTF_CASE1(prefix_hex_zero, "%#x", 0U, "0")
SPRINTF_CASE1(prefix_upper_hex_zero, "%#X", 0U, "0")
SPRINTF_CASE1(prefix_octal_zero, "%#o", 0U, "0")
SPRINTF_CASE1(prefix_binary_zero, "%#b", 0U, "0")
SPRINTF_CASE1(prefix_hex_zero_width, "%#5x", 0U, "    0")
SPRINTF_CASE1(prefix_octal_zero_width, "%#05o", 0U, "00000")

/* Precision is parsed independently from width and flags */
SPRINTF_CASE1(precision_zero_decimal, "%.0d", 0, "0")
SPRINTF_CASE1(precision_dot_only_decimal, "%.d", 0, "0")
SPRINTF_CASE1(precision_dot_only_string, "%.s", "hello", "")
SPRINTF_CASE1(precision_decimal, "%.4d", 12, "0012")
SPRINTF_CASE1(precision_negative_decimal, "%.4d", -12, "-0012")
SPRINTF_CASE1(precision_hex, "%.5x", 0x12U, "00012")
SPRINTF_CASE1(precision_width_decimal, "%8.3d", 12, "     012")
SPRINTF_CASE1(precision_zero_width_decimal, "%08.3d", 12, "00000012")

/* Character and string field widths cover both flush directions */
SPRINTF_CASE1(char_plain, "%c", 'A', "A")
SPRINTF_CASE1(char_width, "%4c", 'A', "   A")
SPRINTF_CASE1(char_left, "%-4c", 'A', "A   ")
SPRINTF_CASE1(char_zero_flag, "%04c", 'A', "   A")
SPRINTF_CASE1(char_width_one, "%1c", 'A', "A")
SPRINTF_CASE1(char_width_zero, "%0c", 'A', "A")
SPRINTF_CASE1(string_plain, "%s", "hello", "hello")
SPRINTF_CASE1(string_width, "%8s", "hello", "   hello")
SPRINTF_CASE1(string_left, "%-8s", "hello", "hello   ")
SPRINTF_CASE1(string_precision, "%.3s", "hello", "hel")
SPRINTF_CASE1(string_zero_precision, "%.0s", "hello", "")
SPRINTF_CASE1(string_width_precision, "%8.3s", "hello", "     hel")
SPRINTF_CASE1(string_left_precision, "%-8.3s", "hello", "hel     ")

/* Dynamic width and precision cover the argument-driven parser branches */
SPRINTF_CASE2(star_width, "%*d", 6, 12, "    12")
SPRINTF_CASE2(star_width_negative, "%*d", -6, 12, "12    ")
SPRINTF_CASE2(star_precision, "%.*d", 4, 12, "0012")
SPRINTF_CASE2(star_precision_negative, "%.*d", -1, 12, "12")
SPRINTF_CASE2(star_string_precision, "%.*s", 3, "hello", "hel")
SPRINTF_CASE2(star_string_width, "%*s", 6, "hi", "    hi")
SPRINTF_CASE3(star_width_precision, "%*.*d", 7, 4, 12, "   0012")
SPRINTF_CASE3(star_width_precision_left, "%*.*d", -7, 4, 12, "0012   ")
SPRINTF_CASE3(star_string_width_precision, "%*.*s", 6, 3, "hello", "   hel")

/* Length modifiers select every integer argument extraction branch */
SPRINTF_CASE1(short_signed, "%hd", (int)-123, "-123")
SPRINTF_CASE1(short_unsigned, "%hu", (unsigned int)65535U, "65535")
SPRINTF_CASE1(char_signed, "%hhd", (int)-12, "-12")
SPRINTF_CASE1(char_unsigned, "%hhu", (unsigned int)250U, "250")
SPRINTF_CASE1(long_signed, "%ld", (long)-123456L, "-123456")
SPRINTF_CASE1(long_unsigned, "%lu", (unsigned long)123456UL, "123456")
SPRINTF_CASE1(long_long_signed, "%lld", (long long)-123456789LL, "-123456789")
SPRINTF_CASE1(long_long_unsigned, "%llu", (unsigned long long)123456789ULL,
              "123456789")
SPRINTF_CASE1(size_signed, "%zd", (mr_ssize_t)-99, "-99")
SPRINTF_CASE1(size_unsigned, "%zu", (mr_size_t)99, "99")

/* Signed zero padding keeps the sign before every inserted zero */
SPRINTF_CASE1(zero_pad_negative_width_six, "%06d", -12, "-00012")
SPRINTF_CASE1(zero_pad_plus_positive, "%+05d", 12, "+0012")
SPRINTF_CASE1(zero_pad_plus_negative, "%+05d", -12, "-0012")
SPRINTF_CASE1(zero_pad_space_positive, "% 05d", 12, " 0012")
SPRINTF_CASE1(zero_pad_space_negative, "% 05d", -12, "-0012")
SPRINTF_CASE1(zero_pad_flag_precedence, "% +05d", 12, "+0012")
SPRINTF_CASE1(zero_pad_left_ignored, "%-05d", -12, "-12  ")
SPRINTF_CASE1(zero_pad_left_plus, "%-+05d", 12, "+12  ")
SPRINTF_CASE1(zero_pad_precision_negative, "%08.3d", -12, "-0000012")
SPRINTF_CASE1(zero_pad_special_decimal, "%#05d", -12, "-0012")
SPRINTF_CASE1(zero_pad_plus_width, "%+08d", 12, "+0000012")
SPRINTF_CASE1(zero_pad_plus_negative_width, "%+08d", -12, "-0000012")
SPRINTF_CASE2(zero_pad_dynamic_width, "%0*d", 5, -12, "-0012")
SPRINTF_CASE2(zero_pad_dynamic_left, "%0*d", -5, 12, "12   ")

MR_TEST(sprintf_matrix, zero_pad_negative_snprintf_truncated) {
    char buffer[4];
    int ret;

    ret = mr_snprintf(buffer, sizeof(buffer), "%05d", -12);
    MR_TEST_ASSERT_EQ_INT(ret, 5);
    MR_TEST_ASSERT_EQ_INT(mr_strcmp(buffer, "-00"), 0);
}

MR_TEST(sprintf_matrix, zero_pad_negative_snprintf_size_one) {
    char buffer[1];
    int ret;

    ret = mr_snprintf(buffer, sizeof(buffer), "%05d", -12);
    MR_TEST_ASSERT_EQ_INT(ret, 5);
    MR_TEST_ASSERT_EQ_INT(buffer[0], '\0');
}

MR_TEST(sprintf_matrix, snprintf_size_zero_preserves_buffer) {
    char buffer[2] = {'X', '\0'};
    int ret;

    ret = mr_snprintf(buffer, 0, "%s", "abc");
    MR_TEST_ASSERT_EQ_INT(ret, 3);
    MR_TEST_ASSERT_EQ_INT(buffer[0], 'X');
    MR_TEST_ASSERT_EQ_INT(buffer[1], '\0');
}

MR_TEST(sprintf_matrix, snprintf_exact_limit) {
    char buffer[4] = {'X', 'X', 'X', 'X'};
    int ret;

    ret = mr_snprintf(buffer, 3, "%s", "abc");
    MR_TEST_ASSERT_EQ_INT(ret, 3);
    MR_TEST_ASSERT_EQ_INT(mr_strcmp(buffer, "ab"), 0);
    MR_TEST_ASSERT_EQ_INT(buffer[3], 'X');
}

MR_TEST(sprintf_matrix, snprintf_compound_truncation) {
    char buffer[6] = {'X', 'X', 'X', 'X', 'X', 'X'};
    int ret;

    ret = mr_snprintf(buffer, sizeof(buffer), "%#x:%s", 0x12U, "abc");
    MR_TEST_ASSERT_EQ_INT(ret, 8);
    MR_TEST_ASSERT_EQ_INT(mr_strcmp(buffer, "0x12:"), 0);
}

MR_TEST(sprintf_matrix, character_null_preserves_following_output) {
    char buffer[4];
    int ret;

    ret = mr_sprintf(buffer, "%cX", '\0');
    MR_TEST_ASSERT_EQ_INT(ret, 2);
    MR_TEST_ASSERT_EQ_INT(buffer[0], '\0');
    MR_TEST_ASSERT_EQ_INT(buffer[1], 'X');
    MR_TEST_ASSERT_EQ_INT(buffer[2], '\0');
}

MR_TEST(sprintf_matrix, pointer_default_shape) {
    char buffer[32];
    int ret;

    ret = mr_sprintf(buffer, "%p", MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ret, (int)((sizeof(void *) << 1) + 2));
    MR_TEST_ASSERT_EQ_INT(buffer[0], '0');
    MR_TEST_ASSERT_EQ_INT(buffer[1], 'x');
    MR_TEST_ASSERT_EQ_INT(buffer[ret - 1], '0');
}

MR_TEST(sprintf_matrix, null_string_width_precision) {
    char buffer[16];
    int ret;

    ret = mr_sprintf(buffer, "%8.4s", MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ret, 8);
    MR_TEST_ASSERT_EQ_INT(mr_strcmp(buffer, "    (nul"), 0);
}

/* Literal percent escapes and unsupported effectors */
SPRINTF_CASE0(empty_format, "", "")
SPRINTF_CASE0(literal_percent, "%%", "%")
SPRINTF_CASE1(unsupported_float, "%f", 1.0, "%f")
SPRINTF_CASE0(unsupported_letter, "%q", "%q")
SPRINTF_CASE0(trailing_percent, "end%", "end%")

#undef SPRINTF_CASE3
#undef SPRINTF_CASE2
#undef SPRINTF_CASE1
#undef SPRINTF_CASE0

#endif /* defined(MR_USE_TEST) && defined(MR_USE_SPRINTF) */
