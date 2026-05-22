/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/test/mr_test.h>
#if defined(MR_USE_TEST)
#include <mrX/libc/mr_sprintf.h>
#include <mrX/libc/mr_string.h>

/* -------------------------------------------------------------------------- */
/* Test: sprintf basic integer formatting                                     */
static void test_sprintf_basic_int(void) {
    char buf[64];
    int ret;

    ret = mr_sprintf(buf, "%d", 123);
    MR_TEST_ASSERT_EQ_INT(ret, 3);
    MR_TEST_ASSERT_EQ_INT(mr_strcmp(buf, "123"), 0);

    ret = mr_sprintf(buf, "%04d", 42);
    MR_TEST_ASSERT_EQ_INT(ret, 4);
    MR_TEST_ASSERT_EQ_INT(mr_strcmp(buf, "0042"), 0);

    ret = mr_sprintf(buf, "%x", 0xAB);
    MR_TEST_ASSERT_EQ_INT(ret, 2);
    MR_TEST_ASSERT_EQ_INT(mr_strcmp(buf, "ab"), 0);
}
MR_TEST_EXPORT(sprintf, basic_int, test_sprintf_basic_int);

/* -------------------------------------------------------------------------- */
/* Test: sprintf string formatting                                            */
static void test_sprintf_string(void) {
    char buf[64];
    int ret;

    ret = mr_sprintf(buf, "%s", "hello");
    MR_TEST_ASSERT_EQ_INT(ret, 5);
    MR_TEST_ASSERT_EQ_INT(mr_strcmp(buf, "hello"), 0);

    ret = mr_sprintf(buf, "%10s", "test");
    MR_TEST_ASSERT_EQ_INT(ret, 10);
    MR_TEST_ASSERT_EQ_INT(mr_strcmp(buf, "      test"), 0);
}
MR_TEST_EXPORT(sprintf, string, test_sprintf_string);

/* -------------------------------------------------------------------------- */
/* Test: sprintf mixed formatting                                             */
static void test_sprintf_mixed(void) {
    char buf[128];
    int ret;

    ret = mr_sprintf(buf, "Val: %d, Str: %s, Hex: %x", 42, "world", 255);
    MR_TEST_ASSERT_EQ_INT(ret, 28);
    MR_TEST_ASSERT_EQ_INT(mr_strcmp(buf, "Val: 42, Str: world, Hex: ff"), 0);
}
MR_TEST_EXPORT(sprintf, mixed, test_sprintf_mixed);

/* -------------------------------------------------------------------------- */
/* Test: snprintf with buffer size limit                                      */
static void test_sprintf_snprintf_limit(void) {
    char buf[10];
    int ret;

    ret = mr_snprintf(buf, sizeof(buf), "%s", "1234567890");
    MR_TEST_ASSERT_EQ_INT(ret, 10);
    MR_TEST_ASSERT_EQ_INT(mr_strcmp(buf, "123456789"), 0);
}
MR_TEST_EXPORT(sprintf, snprintf_limit, test_sprintf_snprintf_limit);

/* -------------------------------------------------------------------------- */
/* Test: sprintf pointer formatting                                          */
static void test_sprintf_pointer(void) {
    const void *ptr = (void *)0x12345678;
    char buf[32];
    int ret;

    ret = mr_sprintf(buf, "%p", ptr);
    MR_TEST_ASSERT_GT_INT(ret, 0);
    MR_TEST_ASSERT_NE_PTR(mr_memchr(buf, '1', (mr_size_t)ret), MR_NULL);
}
MR_TEST_EXPORT(sprintf, pointer, test_sprintf_pointer);

/* -------------------------------------------------------------------------- */
/* Test: sprintf edge cases                                                  */
static void test_sprintf_edge_cases(void) {
    char buf[64];
    int ret;

    ret = mr_sprintf(buf, "");
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(buf[0], '\0');

    ret = mr_sprintf(buf, "%05d", 7);
    MR_TEST_ASSERT_EQ_INT(ret, 5);
    MR_TEST_ASSERT_EQ_INT(mr_strcmp(buf, "00007"), 0);

    ret = mr_sprintf(buf, "%-5d", 42);
    MR_TEST_ASSERT_EQ_INT(ret, 5);
    MR_TEST_ASSERT_EQ_INT(mr_strcmp(buf, "42   "), 0);
}
MR_TEST_EXPORT(sprintf, edge_cases, test_sprintf_edge_cases);

/* -------------------------------------------------------------------------- */
/* Test: sprintf char width formatting                                        */
static void test_sprintf_char_width(void) {
    char buf[16];
    int ret;

    ret = mr_sprintf(buf, "%c", 'A');
    MR_TEST_ASSERT_EQ_INT(ret, 1);
    MR_TEST_ASSERT_EQ_INT(mr_strcmp(buf, "A"), 0);

    ret = mr_sprintf(buf, "%3c", 'A');
    MR_TEST_ASSERT_EQ_INT(ret, 3);
    MR_TEST_ASSERT_EQ_INT(mr_strcmp(buf, "  A"), 0);

    ret = mr_sprintf(buf, "%-3c", 'A');
    MR_TEST_ASSERT_EQ_INT(ret, 3);
    MR_TEST_ASSERT_EQ_INT(mr_strcmp(buf, "A  "), 0);
}
MR_TEST_EXPORT(sprintf, char_width, test_sprintf_char_width);

/* -------------------------------------------------------------------------- */
/* Test: sprintf hh length modifier                                           */
static void test_sprintf_hh_modifier(void) {
    char buf[32];
    int ret;

    ret = mr_sprintf(buf, "%hhd", (signed char)-128);
    MR_TEST_ASSERT_EQ_INT(ret, 4);
    MR_TEST_ASSERT_EQ_INT(mr_strcmp(buf, "-128"), 0);

    ret = mr_sprintf(buf, "%hhu", (unsigned char)255);
    MR_TEST_ASSERT_EQ_INT(ret, 3);
    MR_TEST_ASSERT_EQ_INT(mr_strcmp(buf, "255"), 0);
}
MR_TEST_EXPORT(sprintf, hh_modifier, test_sprintf_hh_modifier);

/* -------------------------------------------------------------------------- */
/* Test: sprintf signed minimum values                                        */
static void test_sprintf_signed_min(void) {
    const mr_int64_t int64_min = -MR_INT64_MAX - 1LL;
    char buf[32];
    int ret;

    ret = mr_sprintf(buf, "%d", MR_INT32_MIN);
    MR_TEST_ASSERT_EQ_INT(ret, 11);
    MR_TEST_ASSERT_EQ_INT(mr_strcmp(buf, "-2147483648"), 0);

    ret = mr_sprintf(buf, "%lld", int64_min);
    MR_TEST_ASSERT_EQ_INT(ret, 20);
    MR_TEST_ASSERT_EQ_INT(mr_strcmp(buf, "-9223372036854775808"), 0);
}
MR_TEST_EXPORT(sprintf, signed_min, test_sprintf_signed_min);

/* -------------------------------------------------------------------------- */
/* Test: snprintf NULL buffer with zero size                                  */
static void test_sprintf_snprintf_null_zero(void) {
    int ret;

    ret = mr_snprintf(MR_NULL, 0, "%d-%s", 123, "abc");
    MR_TEST_ASSERT_EQ_INT(ret, 7);
}
MR_TEST_EXPORT(sprintf, snprintf_null_zero, test_sprintf_snprintf_null_zero);

/* -------------------------------------------------------------------------- */
/* Test: sprintf with NULL string                                              */
static void test_sprintf_null_string(void) {
    char buf[32];
    int ret;

    ret = mr_sprintf(buf, "%s", MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ret, 6);
    MR_TEST_ASSERT_EQ_INT(mr_strcmp(buf, "(null)"), 0);
}
MR_TEST_EXPORT(sprintf, null_string, test_sprintf_null_string);

/* -------------------------------------------------------------------------- */
/* Test: unsigned, octal, binary, percent                                      */
static void test_sprintf_unsigned_octal_bin(void) {
    char buf[64];
    int ret;

    ret = mr_sprintf(buf, "%u", 123U);
    MR_TEST_ASSERT_EQ_INT(ret, 3);
    MR_TEST_ASSERT_EQ_INT(mr_strcmp(buf, "123"), 0);

    ret = mr_sprintf(buf, "%o", 0777);
    MR_TEST_ASSERT_EQ_INT(ret, 3);
    MR_TEST_ASSERT_EQ_INT(mr_strcmp(buf, "777"), 0);

    ret = mr_sprintf(buf, "%b", 10);
    MR_TEST_ASSERT_EQ_INT(ret, 4);
    MR_TEST_ASSERT_EQ_INT(mr_strcmp(buf, "1010"), 0);

    ret = mr_sprintf(buf, "%% %d%%", 50);
    MR_TEST_ASSERT_EQ_INT(ret, 7);
    MR_TEST_ASSERT_EQ_INT(mr_strcmp(buf, "%% 50%%"), 0);
}
MR_TEST_EXPORT(sprintf, unsigned_octal_bin, test_sprintf_unsigned_octal_bin);

/* -------------------------------------------------------------------------- */
/* Test: uppercase hex                                                         */
static void test_sprintf_hex_upper(void) {
    char buf[32];
    int ret;

    ret = mr_sprintf(buf, "%X", 0xdead);
    MR_TEST_ASSERT_EQ_INT(ret, 4);
    MR_TEST_ASSERT_EQ_INT(mr_strcmp(buf, "DEAD"), 0);

    ret = mr_sprintf(buf, "%08X", 0xff);
    MR_TEST_ASSERT_EQ_INT(ret, 8);
    MR_TEST_ASSERT_EQ_INT(mr_strcmp(buf, "000000FF"), 0);
}
MR_TEST_EXPORT(sprintf, hex_upper, test_sprintf_hex_upper);

/* -------------------------------------------------------------------------- */
/* Test: sign flags (+ and space)                                              */
static void test_sprintf_sign_flags(void) {
    char buf[32];
    int ret;

    ret = mr_sprintf(buf, "%+d", 42);
    MR_TEST_ASSERT_EQ_INT(ret, 3);
    MR_TEST_ASSERT_EQ_INT(mr_strcmp(buf, "+42"), 0);

    ret = mr_sprintf(buf, "%+d", -42);
    MR_TEST_ASSERT_EQ_INT(ret, 3);
    MR_TEST_ASSERT_EQ_INT(mr_strcmp(buf, "-42"), 0);

    ret = mr_sprintf(buf, "% d", 42);
    MR_TEST_ASSERT_EQ_INT(ret, 3);
    MR_TEST_ASSERT_EQ_INT(mr_strcmp(buf, " 42"), 0);
}
MR_TEST_EXPORT(sprintf, sign_flags, test_sprintf_sign_flags);

/* -------------------------------------------------------------------------- */
/* Test: special prefix (#) for hex, octal, binary                             */
static void test_sprintf_special_prefix(void) {
    char buf[32];
    int ret;

    ret = mr_sprintf(buf, "%#x", 0xff);
    MR_TEST_ASSERT_EQ_INT(ret, 4);
    MR_TEST_ASSERT_EQ_INT(mr_strcmp(buf, "0xff"), 0);

    ret = mr_sprintf(buf, "%#X", 0xff);
    MR_TEST_ASSERT_EQ_INT(ret, 4);
    MR_TEST_ASSERT_EQ_INT(mr_strcmp(buf, "0XFF"), 0);

    ret = mr_sprintf(buf, "%#o", 0777);
    MR_TEST_ASSERT_EQ_INT(ret, 4);
    MR_TEST_ASSERT_EQ_INT(mr_strcmp(buf, "0777"), 0);

    ret = mr_sprintf(buf, "%#b", 10);
    MR_TEST_ASSERT_EQ_INT(ret, 6);
    MR_TEST_ASSERT_EQ_INT(mr_strcmp(buf, "0b1010"), 0);
}
MR_TEST_EXPORT(sprintf, special_prefix, test_sprintf_special_prefix);

/* -------------------------------------------------------------------------- */
/* Test: precision for integer and string                                      */
static void test_sprintf_precision(void) {
    char buf[32];
    int ret;

    ret = mr_sprintf(buf, "%.4d", 7);
    MR_TEST_ASSERT_EQ_INT(ret, 4);
    MR_TEST_ASSERT_EQ_INT(mr_strcmp(buf, "0007"), 0);

    ret = mr_sprintf(buf, "%.*s", 3, "hello");
    MR_TEST_ASSERT_EQ_INT(ret, 3);
    MR_TEST_ASSERT_EQ_INT(mr_strcmp(buf, "hel"), 0);
}
MR_TEST_EXPORT(sprintf, precision, test_sprintf_precision);

/* -------------------------------------------------------------------------- */
/* Test: asterisk width (positive and negative)                                */
static void test_sprintf_asterisk_width(void) {
    char buf[32];
    int ret;

    ret = mr_sprintf(buf, "%*d", 8, 123);
    MR_TEST_ASSERT_EQ_INT(ret, 8);
    MR_TEST_ASSERT_EQ_INT(mr_strcmp(buf, "     123"), 0);

    ret = mr_sprintf(buf, "%*d", -8, 123);
    MR_TEST_ASSERT_EQ_INT(ret, 8);
    MR_TEST_ASSERT_EQ_INT(mr_strcmp(buf, "123     "), 0);
}
MR_TEST_EXPORT(sprintf, asterisk_width, test_sprintf_asterisk_width);

/* -------------------------------------------------------------------------- */
/* Test: long modifier (l)                                                     */
static void test_sprintf_long_modifier(void) {
    char buf[32];
    int ret;

    ret = mr_sprintf(buf, "%ld", 123456L);
    MR_TEST_ASSERT_EQ_INT(ret, 6);
    MR_TEST_ASSERT_EQ_INT(mr_strcmp(buf, "123456"), 0);

    ret = mr_sprintf(buf, "%lx", 0xabcdefUL);
    MR_TEST_ASSERT_EQ_INT(ret, 6);
    MR_TEST_ASSERT_EQ_INT(mr_strcmp(buf, "abcdef"), 0);
}
MR_TEST_EXPORT(sprintf, long_modifier, test_sprintf_long_modifier);

/* -------------------------------------------------------------------------- */
/* Test: short modifier (h)                                                    */
static void test_sprintf_short_modifier(void) {
    char buf[32];
    int ret;

    ret = mr_sprintf(buf, "%hd", (short)-12345);
    MR_TEST_ASSERT_EQ_INT(ret, 6);
    MR_TEST_ASSERT_EQ_INT(mr_strcmp(buf, "-12345"), 0);

    ret = mr_sprintf(buf, "%hu", (unsigned short)0xabcd);
    MR_TEST_ASSERT_EQ_INT(ret, 5);
    MR_TEST_ASSERT_EQ_INT(mr_strcmp(buf, "43981"), 0);
}
MR_TEST_EXPORT(sprintf, short_modifier, test_sprintf_short_modifier);

/* -------------------------------------------------------------------------- */
/* Test: mr_snprintf with NULL format                                          */
static void test_sprintf_snprintf_null_fmt(void) {
    char buf[32];
    int ret;

    ret = mr_snprintf(buf, sizeof(buf), MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
}
MR_TEST_EXPORT(sprintf, snprintf_null_fmt, test_sprintf_snprintf_null_fmt);

/* -------------------------------------------------------------------------- */
/* Test: mr_vsprintf via wrapper helper                                        */
static int vsprintf_wrapper(char *buf, const char *fmt, ...) {
    mr_va_list args;
    int ret;

    mr_va_start(args, fmt);
    ret = mr_vsprintf(buf, fmt, args);
    mr_va_end(args);
    return ret;
}

static void test_sprintf_vsprintf(void) {
    char buf[64];
    int ret;

    ret = vsprintf_wrapper(buf, "%d_%s_%x", 42, "test", 0xff);
    MR_TEST_ASSERT_EQ_INT(ret, 10);
    MR_TEST_ASSERT_EQ_INT(mr_strcmp(buf, "42_test_ff"), 0);
}
MR_TEST_EXPORT(sprintf, vsprintf, test_sprintf_vsprintf);

/* -------------------------------------------------------------------------- */
/* Test: snprintf with size=1 (only null terminator)                           */
static void test_sprintf_snprintf_size1(void) {
    char buf[1];
    int ret;

    ret = mr_snprintf(buf, 1, "%d", 123);
    MR_TEST_ASSERT_EQ_INT(ret, 3);
    MR_TEST_ASSERT_EQ_INT(buf[0], '\0');
}
MR_TEST_EXPORT(sprintf, snprintf_size1, test_sprintf_snprintf_size1);
#endif /* defined(MR_USE_TEST) */
