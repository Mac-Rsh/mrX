/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2026-08-30        MacRsh        First version
 */

#include <mrX/test/mr_test.h>
#if defined(MR_USE_TEST)
#if defined(MR_USE_PRINTF)
#include <mrX/kernel/mr_printf.h>

/* Helpers */
static int vprintf_wrapper(const char *fmt, ...) {
    mr_va_list args;
    int ret;

    mr_va_start(args, fmt);
    ret = mr_vprintf(fmt, args);
    mr_va_end(args);
    return ret;
}

/* Tests */

/* The return value of mr_printf equals the number of characters actually
 * emitted, so exact lengths can be verified without capturing the output
 */
MR_TEST(printf, percent_percent) {
    /* Documented implementation behavior: "%%" echoes two percent signs
     * instead of the standard single one (see document/libc/sprintf.md)
     */
    MR_TEST_ASSERT_EQ_INT(mr_printf("%%"), 2);
    MR_TEST_ASSERT_EQ_INT(mr_printf("a%%b"), 4);
    MR_TEST_ASSERT_EQ_INT(mr_printf("%%%%"), 4);
}

MR_TEST(printf, unknown_and_trailing_specifier) {
    /* Unknown specifiers echo the conversion verbatim */
    MR_TEST_ASSERT_EQ_INT(mr_printf("%q"), 2);
    /* Width is ignored for unknown specifiers */
    MR_TEST_ASSERT_EQ_INT(mr_printf("%5q"), 2);
    /* A lone effector at end of format must not read past the terminator */
    MR_TEST_ASSERT_EQ_INT(mr_printf("%z"), 1);
    MR_TEST_ASSERT_EQ_INT(mr_printf("%l"), 1);
    /* A trailing single percent must be safe and emit one percent */
    MR_TEST_ASSERT_EQ_INT(mr_printf("abc%"), 4);
}

MR_TEST(printf, null_string) {
    /* NULL string argument prints "(null)" */
    MR_TEST_ASSERT_EQ_INT(mr_printf("%s", MR_NULL), 6);
    MR_TEST_ASSERT_EQ_INT(mr_printf("%s|%s", MR_NULL, "abc"), 10);
    MR_TEST_ASSERT_EQ_INT(mr_printf("%10s", MR_NULL), 10);
}

MR_TEST(printf, string_precision) {
    MR_TEST_ASSERT_EQ_INT(mr_printf("%.3s", "abcdef"), 3);
    MR_TEST_ASSERT_EQ_INT(mr_printf("%.0s", "abc"), 0);
    MR_TEST_ASSERT_EQ_INT(mr_printf("%.*s", 2, "abcdef"), 2);
    MR_TEST_ASSERT_EQ_INT(mr_printf("%8.3s", "abcdef"), 8);
    MR_TEST_ASSERT_EQ_INT(mr_printf("%-8.3s|", "abcdef"), 9);
}

MR_TEST(printf, string_width) {
    MR_TEST_ASSERT_EQ_INT(mr_printf("%8s", "abc"), 8);
    MR_TEST_ASSERT_EQ_INT(mr_printf("%-8s|", "abc"), 9);
    /* Width smaller than the string must not truncate it */
    MR_TEST_ASSERT_EQ_INT(mr_printf("%2s", "hello"), 5);
}

MR_TEST(printf, star_width) {
    /* Negative star width means left align with the absolute value */
    MR_TEST_ASSERT_EQ_INT(mr_printf("%*d|", -5, 42), 6);
    MR_TEST_ASSERT_EQ_INT(mr_printf("%*d", 6, 42), 6);
    /* Note: huge star width or precision values are intentionally not
     * tested, they would drive multi-billion iteration fill loops
     */
}

MR_TEST(printf, int_boundaries) {
    MR_TEST_ASSERT_EQ_INT(mr_printf("%d", (-2147483647 - 1)), 11);
    MR_TEST_ASSERT_EQ_INT(mr_printf("%d", 2147483647), 10);
    MR_TEST_ASSERT_EQ_INT(mr_printf("%u", 4294967295U), 10);
    MR_TEST_ASSERT_EQ_INT(mr_printf("%d", 0), 1);
    MR_TEST_ASSERT_EQ_INT(mr_printf("%u", 0U), 1);
}

MR_TEST(printf, int64_boundaries) {
    MR_TEST_ASSERT_EQ_INT(mr_printf("%lld", (-9223372036854775807LL - 1)), 20);
    MR_TEST_ASSERT_EQ_INT(mr_printf("%lld", 9223372036854775807LL), 19);
    MR_TEST_ASSERT_EQ_INT(mr_printf("%llu", 18446744073709551615ULL), 20);
}

MR_TEST(printf, sign_and_zero_pad) {
    /* Zero pad keeps the sign ahead of the padding zeros */
    MR_TEST_ASSERT_EQ_INT(mr_printf("%08d", -42), 8);
    MR_TEST_ASSERT_EQ_INT(mr_printf("%08x", 0xab), 8);
    MR_TEST_ASSERT_EQ_INT(mr_printf("%+d", 42), 3);
    MR_TEST_ASSERT_EQ_INT(mr_printf("% d", 42), 3);
    MR_TEST_ASSERT_EQ_INT(mr_printf("%+d", -42), 3);
    /* Left align clears the zero pad flag */
    MR_TEST_ASSERT_EQ_INT(mr_printf("%-08d|", 42), 9);
}

MR_TEST(printf, int_width_precision) {
    MR_TEST_ASSERT_EQ_INT(mr_printf("%8.4d", 42), 8);
    MR_TEST_ASSERT_EQ_INT(mr_printf("%-8.4d|", 42), 9);
    MR_TEST_ASSERT_EQ_INT(mr_printf("%.5d", 42), 5);
    MR_TEST_ASSERT_EQ_INT(mr_printf("%.0d", 5), 1);
}

MR_TEST(printf, precision_star_negative) {
    int ret;

    /* Potential bug: a negative precision for %.*s must be treated as an
     * omitted precision per the C standard, but mr_vsnprintf clamps it to
     * zero and prints nothing
     */
    ret = mr_printf("%.*s|", -1, "abc");
    MR_TEST_EXPECT_EQ_INT(ret, 4);

    /* Potential bug: a bare "." precision must be treated as precision zero
     * per the C standard, but mr_vsnprintf keeps it at minus one and prints
     * the full string
     */
    ret = mr_printf("%.s|", "abc");
    MR_TEST_EXPECT_EQ_INT(ret, 1);
}

MR_TEST(printf, precision_zero_zero_value) {
    int ret;

    /* Project formatting keeps one digit for zero at precision zero */
    ret = mr_printf("%.0d", 0);
    MR_TEST_EXPECT_EQ_INT(ret, 1);
}

MR_TEST(printf, truncation_boundary) {
    /* MR_CFG_PRINTF_BUF_SIZE is 64, so at most 63 characters are emitted */
    MR_TEST_ASSERT_EQ_INT(mr_printf("%62s", "A"), 62);
    MR_TEST_ASSERT_EQ_INT(mr_printf("%63s", "A"), 63);
    MR_TEST_ASSERT_EQ_INT(mr_printf("%64s", "A"), 63);
    MR_TEST_ASSERT_EQ_INT(mr_printf("%200s", "A"), 63);
    MR_TEST_ASSERT_EQ_INT(mr_printf("%s",
                                    "0123456789012345678901234567890123456789"
                                    "0123456789012345678901234567890123456789"
                                    "012345678901234567890"), 63);
}

MR_TEST(printf, pointer_format) {
    int local;

    /* Default pointer format is "0x" plus 16 hex digits on x64, so the
     * emitted length is 18 regardless of the pointer value
     */
    MR_TEST_ASSERT_EQ_INT(mr_printf("%p", MR_NULL), 18);
    MR_TEST_ASSERT_EQ_INT(mr_printf("%p", (void *)&local), 18);
}

MR_TEST(printf, char_and_bases) {
    MR_TEST_ASSERT_EQ_INT(mr_printf("%c", 'A'), 1);
    MR_TEST_ASSERT_EQ_INT(mr_printf("%5c", 'A'), 5);
    MR_TEST_ASSERT_EQ_INT(mr_printf("%-5c|", 'A'), 6);
    MR_TEST_ASSERT_EQ_INT(mr_printf("%o", 8), 2);
    MR_TEST_ASSERT_EQ_INT(mr_printf("%#o", 8), 3);
    MR_TEST_ASSERT_EQ_INT(mr_printf("%b", 5), 3);
    MR_TEST_ASSERT_EQ_INT(mr_printf("%#b", 5), 5);
    MR_TEST_ASSERT_EQ_INT(mr_printf("%x", 0), 1);
    /* Zero value clears the alternate form prefix */
    MR_TEST_ASSERT_EQ_INT(mr_printf("%#x", 0), 1);
    MR_TEST_ASSERT_EQ_INT(mr_printf("%X", 0xdeadbeefU), 8);
}

MR_TEST(printf, unsupported_float) {
    /* Float conversions are unsupported and echo the specifier verbatim,
     * the double argument must still be consumed from the va_list
     */
    MR_TEST_ASSERT_EQ_INT(mr_printf("%f|%d", 1.5, 7), 4);
}

MR_TEST(printf, length_modifiers) {
    MR_TEST_ASSERT_EQ_INT(mr_printf("%hhd", -1), 2);
    MR_TEST_ASSERT_EQ_INT(mr_printf("%hhu", 300), 2);
    MR_TEST_ASSERT_EQ_INT(mr_printf("%hd", 70000), 4);
    MR_TEST_ASSERT_EQ_INT(mr_printf("%lu", 100000UL), 6);
    MR_TEST_ASSERT_EQ_INT(mr_printf("%zu", (mr_size_t)42), 2);
}

MR_TEST(printf, vprintf_truncation_boundary) {
    MR_TEST_ASSERT_EQ_INT(vprintf_wrapper("%200s", "A"), 63);
    MR_TEST_ASSERT_EQ_INT(vprintf_wrapper("%64s|", "A"), 63);
}

#endif /* defined(MR_USE_PRINTF) */
#endif /* defined(MR_USE_TEST) */
