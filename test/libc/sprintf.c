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
#include <limits.h>

/* -------------------------------------------------------------------------- */
/* Test: sprintf basic integer formatting                                     */
MR_TEST(sprintf, basic_int) {
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

/* -------------------------------------------------------------------------- */
/* Test: sprintf string formatting                                            */
MR_TEST(sprintf, string) {
    char buf[64];
    int ret;

    ret = mr_sprintf(buf, "%s", "hello");
    MR_TEST_ASSERT_EQ_INT(ret, 5);
    MR_TEST_ASSERT_EQ_INT(mr_strcmp(buf, "hello"), 0);

    ret = mr_sprintf(buf, "%10s", "test");
    MR_TEST_ASSERT_EQ_INT(ret, 10);
    MR_TEST_ASSERT_EQ_INT(mr_strcmp(buf, "      test"), 0);
}

/* -------------------------------------------------------------------------- */
/* Test: sprintf mixed formatting                                             */
MR_TEST(sprintf, mixed) {
    char buf[128];
    int ret;

    ret = mr_sprintf(buf, "Val: %d, Str: %s, Hex: %x", 42, "world", 255);
    MR_TEST_ASSERT_EQ_INT(ret, 28);
    MR_TEST_ASSERT_EQ_INT(mr_strcmp(buf, "Val: 42, Str: world, Hex: ff"), 0);
}

/* -------------------------------------------------------------------------- */
/* Test: snprintf with buffer size limit                                      */
MR_TEST(sprintf, snprintf_limit) {
    char buf[10];
    int ret;

    ret = mr_snprintf(buf, sizeof(buf), "%s", "1234567890");
    MR_TEST_ASSERT_EQ_INT(ret, 10);
    MR_TEST_ASSERT_EQ_INT(mr_strcmp(buf, "123456789"), 0);
}

/* -------------------------------------------------------------------------- */
/* Test: sprintf pointer formatting                                          */
MR_TEST(sprintf, pointer) {
    const void *ptr = (void *)0x12345678;
    char buf[32];
    int ret;

    ret = mr_sprintf(buf, "%p", ptr);
    MR_TEST_ASSERT_GT_INT(ret, 0);
    MR_TEST_ASSERT_NE_PTR(mr_memchr(buf, '1', (mr_size_t)ret), MR_NULL);
}

/* -------------------------------------------------------------------------- */
/* Test: sprintf edge cases                                                  */
MR_TEST(sprintf, edge_cases) {
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

/* -------------------------------------------------------------------------- */
/* Test: sprintf char width formatting                                        */
MR_TEST(sprintf, char_width) {
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

/* -------------------------------------------------------------------------- */
/* Test: sprintf hh length modifier                                           */
MR_TEST(sprintf, hh_modifier) {
    char buf[32];
    int ret;

    ret = mr_sprintf(buf, "%hhd", (signed char)-128);
    MR_TEST_ASSERT_EQ_INT(ret, 4);
    MR_TEST_ASSERT_EQ_INT(mr_strcmp(buf, "-128"), 0);

    ret = mr_sprintf(buf, "%hhu", (unsigned char)255);
    MR_TEST_ASSERT_EQ_INT(ret, 3);
    MR_TEST_ASSERT_EQ_INT(mr_strcmp(buf, "255"), 0);
}

/* -------------------------------------------------------------------------- */
/* Test: sprintf signed minimum values                                        */
MR_TEST(sprintf, signed_min) {
    const long long int64_min = LLONG_MIN;
    char buf[32];
    int ret;

    ret = mr_sprintf(buf, "%d", INT_MIN);
    MR_TEST_ASSERT_EQ_INT(ret, 11);
    MR_TEST_ASSERT_EQ_INT(mr_strcmp(buf, "-2147483648"), 0);

    ret = mr_sprintf(buf, "%lld", int64_min);
    MR_TEST_ASSERT_EQ_INT(ret, 20);
    MR_TEST_ASSERT_EQ_INT(mr_strcmp(buf, "-9223372036854775808"), 0);
}

/* -------------------------------------------------------------------------- */
/* Test: sprintf exact integer argument types */
MR_TEST(sprintf, integer_arguments) {
#if LONG_MAX == 2147483647L
    const char expected[]
        = "-2147483648|4294967295|-128|255|-32768|65535|-2147483648|"
          "4294967295|-9223372036854775808|18446744073709551615";
#else
    const char expected[] = "-2147483648|4294967295|-128|255|-32768|65535|"
                            "-9223372036854775808|18446744073709551615|"
                            "-9223372036854775808|18446744073709551615";
#endif
    char buf[192];
    int ret;

    ret = mr_sprintf(buf, "%d|%u|%hhd|%hhu|%hd|%hu|%ld|%lu|%lld|%llu", INT_MIN,
                     UINT_MAX, SCHAR_MIN, (unsigned int)UCHAR_MAX, SHRT_MIN,
                     (unsigned int)USHRT_MAX, LONG_MIN, ULONG_MAX, LLONG_MIN,
                     ULLONG_MAX);
    MR_TEST_ASSERT_EQ_INT(ret, (int)(sizeof(expected) - 1));
    MR_TEST_ASSERT_EQ_INT(mr_strcmp(buf, expected), 0);

#if defined(MR_USE_ARCH_64B)
    ret = mr_sprintf(buf, "%zd|%zu", (mr_ssize_t)LLONG_MIN,
                     (mr_size_t)MR_SIZE_MAX);
    MR_TEST_ASSERT_EQ_INT(ret, 41);
    MR_TEST_ASSERT_EQ_INT(
        mr_strcmp(buf, "-9223372036854775808|18446744073709551615"), 0);
#else
    ret = mr_sprintf(buf, "%zd|%zu", (mr_ssize_t)INT_MIN,
                     (mr_size_t)MR_SIZE_MAX);
    MR_TEST_ASSERT_EQ_INT(ret, 22);
    MR_TEST_ASSERT_EQ_INT(mr_strcmp(buf, "-2147483648|4294967295"), 0);
#endif /* defined(MR_USE_ARCH_64B) */

    ret = mr_sprintf(buf, "%d|%llu|%hd|%lu|%hhu|%zd|%x", -7,
                     18446744073709551615ULL, -123, 123456UL, 250U,
                     (mr_ssize_t)-99, 0xabU);
    MR_TEST_ASSERT_EQ_INT(ret, 46);
    MR_TEST_ASSERT_EQ_INT(
        mr_strcmp(buf, "-7|18446744073709551615|-123|123456|250|-99|ab"), 0);
}

/* -------------------------------------------------------------------------- */
/* Test: snprintf NULL buffer with zero size                                  */
MR_TEST(sprintf, snprintf_null_zero) {
    int ret;

    ret = mr_snprintf(MR_NULL, 0, "%d-%s", 123, "abc");
    MR_TEST_ASSERT_EQ_INT(ret, 7);
}

/* -------------------------------------------------------------------------- */
/* Test: sprintf with NULL string */
MR_TEST(sprintf, null_string) {
    char buf[32];
    int ret;

    ret = mr_sprintf(buf, "%s", MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ret, 6);
    MR_TEST_ASSERT_EQ_INT(mr_strcmp(buf, "(null)"), 0);
}

/* -------------------------------------------------------------------------- */
/* Test: unsigned, octal, binary, percent */
MR_TEST(sprintf, unsigned_octal_bin) {
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

/* -------------------------------------------------------------------------- */
/* Test: uppercase hex */
MR_TEST(sprintf, hex_upper) {
    char buf[32];
    int ret;

    ret = mr_sprintf(buf, "%X", 0xdead);
    MR_TEST_ASSERT_EQ_INT(ret, 4);
    MR_TEST_ASSERT_EQ_INT(mr_strcmp(buf, "DEAD"), 0);

    ret = mr_sprintf(buf, "%08X", 0xff);
    MR_TEST_ASSERT_EQ_INT(ret, 8);
    MR_TEST_ASSERT_EQ_INT(mr_strcmp(buf, "000000FF"), 0);
}

/* -------------------------------------------------------------------------- */
/* Test: sign flags (+ and space) */
MR_TEST(sprintf, sign_flags) {
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

/* -------------------------------------------------------------------------- */
/* Test: special prefix (#) for hex, octal, binary */
MR_TEST(sprintf, special_prefix) {
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

/* -------------------------------------------------------------------------- */
/* Test: precision for integer and string */
MR_TEST(sprintf, precision) {
    char buf[32];
    int ret;

    ret = mr_sprintf(buf, "%.4d", 7);
    MR_TEST_ASSERT_EQ_INT(ret, 4);
    MR_TEST_ASSERT_EQ_INT(mr_strcmp(buf, "0007"), 0);

    ret = mr_sprintf(buf, "%.*s", 3, "hello");
    MR_TEST_ASSERT_EQ_INT(ret, 3);
    MR_TEST_ASSERT_EQ_INT(mr_strcmp(buf, "hel"), 0);
}

/* -------------------------------------------------------------------------- */
/* Test: asterisk width (positive and negative) */
MR_TEST(sprintf, asterisk_width) {
    char buf[32];
    int ret;

    ret = mr_sprintf(buf, "%*d", 8, 123);
    MR_TEST_ASSERT_EQ_INT(ret, 8);
    MR_TEST_ASSERT_EQ_INT(mr_strcmp(buf, "     123"), 0);

    ret = mr_sprintf(buf, "%*d", -8, 123);
    MR_TEST_ASSERT_EQ_INT(ret, 8);
    MR_TEST_ASSERT_EQ_INT(mr_strcmp(buf, "123     "), 0);
}

/* -------------------------------------------------------------------------- */
/* Test: long modifier (l) */
MR_TEST(sprintf, long_modifier) {
    char buf[32];
    int ret;

    ret = mr_sprintf(buf, "%ld", 123456L);
    MR_TEST_ASSERT_EQ_INT(ret, 6);
    MR_TEST_ASSERT_EQ_INT(mr_strcmp(buf, "123456"), 0);

    ret = mr_sprintf(buf, "%lx", 0xabcdefUL);
    MR_TEST_ASSERT_EQ_INT(ret, 6);
    MR_TEST_ASSERT_EQ_INT(mr_strcmp(buf, "abcdef"), 0);
}

/* -------------------------------------------------------------------------- */
/* Test: short modifier (h) */
MR_TEST(sprintf, short_modifier) {
    char buf[32];
    int ret;

    ret = mr_sprintf(buf, "%hd", (short)-12345);
    MR_TEST_ASSERT_EQ_INT(ret, 6);
    MR_TEST_ASSERT_EQ_INT(mr_strcmp(buf, "-12345"), 0);

    ret = mr_sprintf(buf, "%hu", (unsigned short)0xabcd);
    MR_TEST_ASSERT_EQ_INT(ret, 5);
    MR_TEST_ASSERT_EQ_INT(mr_strcmp(buf, "43981"), 0);
}

/* -------------------------------------------------------------------------- */
/* Test: mr_snprintf with NULL format */
MR_TEST(sprintf, snprintf_null_fmt) {
    char buf[32];
    int ret;

    ret = mr_snprintf(buf, sizeof(buf), MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
}

/* -------------------------------------------------------------------------- */
/* Test: mr_vsprintf via wrapper helper */
static int vsprintf_wrapper(char *buf, const char *fmt, ...) {
    mr_va_list args;
    int ret;

    mr_va_start(args, fmt);
    ret = mr_vsprintf(buf, fmt, args);
    mr_va_end(args);
    return ret;
}

MR_TEST(sprintf, vsprintf) {
    char buf[64];
    int ret;

    ret = vsprintf_wrapper(buf, "%d_%s_%x", 42, "test", 0xff);
    MR_TEST_ASSERT_EQ_INT(ret, 10);
    MR_TEST_ASSERT_EQ_INT(mr_strcmp(buf, "42_test_ff"), 0);
}

/* -------------------------------------------------------------------------- */
/* Test: snprintf with size=1 (only null terminator) */
MR_TEST(sprintf, snprintf_size1) {
    char buf[1];
    int ret;

    ret = mr_snprintf(buf, 1, "%d", 123);
    MR_TEST_ASSERT_EQ_INT(ret, 3);
    MR_TEST_ASSERT_EQ_INT(buf[0], '\0');
}

#endif /* defined(MR_USE_TEST) */
