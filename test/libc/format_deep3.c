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

/* Test: every flag combined on one hex conversion left justifies             */
MR_TEST(sprintf, fmt3_flag_kitchen_hex) {
    char buf[32];
    int ret;

    /* '#' adds the 0x prefix, '-' clears '0', '+' and ' ' are inert for
     * unsigned conversions, precision 4 zero extends the two digits
     */
    ret = mr_sprintf(buf, "%-+ #012.4x", 0xabU);
    MR_TEST_ASSERT_EQ_INT(ret, 12);
    MR_TEST_ASSERT_EQ_INT(mr_strcmp(buf, "0x00ab      "), 0);
}

/* Test: plus flag wins over the space flag for signed output                 */
MR_TEST(sprintf, fmt3_both_sign_flags) {
    char buf[32];
    int ret;

    ret = mr_sprintf(buf, "% +d|% +05d", 12, -12);
    MR_TEST_ASSERT_EQ_INT(ret, 9);
    MR_TEST_ASSERT_EQ_INT(mr_strcmp(buf, "+12|-0012"), 0);
}

/* Test: negative star width combined with negative and positive precision    */
MR_TEST(sprintf, fmt3_star_neg_width_prec) {
    char buf[32];
    int ret;

    /* A negative star width means left justify with the absolute width */
    ret = mr_sprintf(buf, "%*.*d|%0*.*d", -8, -3, 12, -8, 4, 12);
    MR_TEST_ASSERT_EQ_INT(ret, 17);
    MR_TEST_ASSERT_EQ_INT(mr_strcmp(buf, "12      |0012    "), 0);
}

/* Test: uppercase hex with alternate prefix zero pad and precision at once   */
MR_TEST(sprintf, fmt3_upper_hex_all) {
    char buf[32];
    int ret;

    ret = mr_sprintf(buf, "%#012.6X", 0xabU);
    MR_TEST_ASSERT_EQ_INT(ret, 12);
    MR_TEST_ASSERT_EQ_INT(mr_strcmp(buf, "0X00000000AB"), 0);
}

/* Test: octal alternate form of zero with a large precision                  */
MR_TEST(sprintf, fmt3_octal_hash_big_prec_zero) {
    char buf[32];
    int ret;

    /* C99: '#o' only forces the leading zero when precision does not
     * already supply one, so ten zeros are emitted without an extra digit
     */
    ret = mr_sprintf(buf, "%#.10o", 0U);
    MR_TEST_ASSERT_EQ_INT(ret, 10);
    MR_TEST_ASSERT_EQ_INT(mr_strcmp(buf, "0000000000"), 0);
}

/* Test: zero character with field width and left justification               */
MR_TEST(sprintf, fmt3_char_zero_width) {
    char buf[16];
    int ret;

    ret = mr_sprintf(buf, "%6c", '\0');
    MR_TEST_ASSERT_EQ_INT(ret, 6);
    MR_TEST_ASSERT_EQ_INT(buf[0], ' ');
    MR_TEST_ASSERT_EQ_INT(buf[4], ' ');
    MR_TEST_ASSERT_EQ_INT(buf[5], '\0');

    ret = mr_sprintf(buf, "%-6c", 'Z');
    MR_TEST_ASSERT_EQ_INT(ret, 6);
    MR_TEST_ASSERT_EQ_INT(mr_strcmp(buf, "Z     "), 0);
}

/* Test: string precision still stops before an embedded null                 */
MR_TEST(sprintf, fmt3_string_prec_embedded_nul) {
    char src[6] = {'a', 'b', '\0', 'c', 'd', 'e'};
    char buf[16];
    int ret;

    mr_memset(buf, 0xaa, sizeof(buf));

    /* Precision limits the scan but does not include a terminating null */
    ret = mr_sprintf(buf, "%.5s", src);
    MR_TEST_EXPECT_EQ_INT(ret, 2);
    MR_TEST_EXPECT_EQ_INT(buf[0], 'a');
    MR_TEST_EXPECT_EQ_INT(buf[1], 'b');
    MR_TEST_EXPECT_EQ_INT(buf[2], '\0');
    MR_TEST_EXPECT_EQ_UINT((mr_uint8_t)buf[3], 0xaaU);
}

/* Test: pointer with explicit width and left justification                   */
MR_TEST(sprintf, fmt3_pointer_left_width) {
    const void *ptr = (void *)0x1234;
    char buf[32];
    int ret;

    /* The pointer representation is implementation defined, this engine
     * prints raw hex without a prefix when a width is spelled out
     */
    ret = mr_sprintf(buf, "%-20p", ptr);
    MR_TEST_ASSERT_EQ_INT(ret, 20);
    MR_TEST_ASSERT_EQ_INT(buf[0], '1');
    MR_TEST_ASSERT_EQ_INT(buf[3], '4');
    MR_TEST_ASSERT_EQ_INT(buf[4], ' ');
    MR_TEST_ASSERT_EQ_INT(buf[19], ' ');
    MR_TEST_ASSERT_EQ_INT(buf[20], '\0');
}

/* Test: half and byte length modifiers wrap the narrowed argument            */
MR_TEST(sprintf, fmt3_length_wrap_hd) {
    char buf[32];
    int ret;

    /* INT_MIN + 32768 = 0x80008000 narrows to short -32768, and the
     * unsigned byte conversion of -1 narrows to 255
     */
    ret = mr_sprintf(buf, "%hd|%hhu", INT_MIN + 32768, (unsigned int)-1);
    MR_TEST_ASSERT_EQ_INT(ret, 10);
    MR_TEST_ASSERT_EQ_INT(mr_strcmp(buf, "-32768|255"), 0);
}

/* Test: long long boundaries across signed decimal and hex                   */
MR_TEST(sprintf, fmt3_ll_boundaries) {
    char buf[64];
    int ret;

    ret = mr_sprintf(buf, "%llu|%lld|%llx", ULLONG_MAX, LLONG_MIN,
                     (unsigned long long)4294967296ULL);
    MR_TEST_ASSERT_EQ_INT(ret, 51);
    MR_TEST_ASSERT_EQ_INT(mr_strcmp(buf,
        "18446744073709551615|-9223372036854775808|100000000"), 0);
}

/* Test: an unknown conversion between valid ones consumes no argument        */
MR_TEST(sprintf, fmt3_arg_discipline_unknown) {
    char buf[32];
    int ret;

    /* Undefined in C99, this engine echoes %q verbatim and keeps the two
     * valid conversions aligned with the two supplied arguments
     */
    ret = mr_sprintf(buf, "%d%q%d", 1, 2);
    MR_TEST_ASSERT_EQ_INT(ret, 4);
    MR_TEST_ASSERT_EQ_INT(mr_strcmp(buf, "1%q2"), 0);
}

/* Test: successive star widths each consume one argument                     */
MR_TEST(sprintf, fmt3_star_successive) {
    char buf[32];
    int ret;

    ret = mr_sprintf(buf, "%*d|%*d", 6, 12, 7, -12);
    MR_TEST_ASSERT_EQ_INT(ret, 14);
    MR_TEST_ASSERT_EQ_INT(mr_strcmp(buf, "    12|    -12"), 0);
}

/* Test: literal characters around the doubled percent escape                 */
MR_TEST(sprintf, fmt3_literal_between) {
    char buf[32];
    int ret;

    /* Documented deviation: %% echoes two percents instead of one, so the
     * trailing d stays a literal character and no argument is consumed
     */
    ret = mr_sprintf(buf, "%d%%d", 7);
    MR_TEST_ASSERT_EQ_INT(ret, 4);
    MR_TEST_ASSERT_EQ_INT(mr_strcmp(buf, "7%%d"), 0);
}

/* Test: snprintf return value at every truncation boundary                   */
MR_TEST(sprintf, fmt3_snprintf_boundary) {
    char buf[8];
    int ret;

    ret = mr_snprintf(MR_NULL, 0, "%d", 42);
    MR_TEST_ASSERT_EQ_INT(ret, 2);

    ret = mr_snprintf(buf, 1, "abcdef");
    MR_TEST_ASSERT_EQ_INT(ret, 6);
    MR_TEST_ASSERT_EQ_INT(buf[0], '\0');

    ret = mr_snprintf(buf, 5, "abcdef");
    MR_TEST_ASSERT_EQ_INT(ret, 6);
    MR_TEST_ASSERT_EQ_INT(mr_strcmp(buf, "abcd"), 0);

    ret = mr_snprintf(buf, 6, "abcdef");
    MR_TEST_ASSERT_EQ_INT(ret, 6);
    MR_TEST_ASSERT_EQ_INT(mr_strcmp(buf, "abcde"), 0);

    ret = mr_snprintf(buf, 7, "abcdef");
    MR_TEST_ASSERT_EQ_INT(ret, 6);
    MR_TEST_ASSERT_EQ_INT(mr_strcmp(buf, "abcdef"), 0);
    MR_TEST_ASSERT_EQ_INT(buf[6], '\0');
}

#endif /* defined(MR_USE_SPRINTF) */

#if defined(MR_USE_SSCANF)
#include <mrX/libc/mr_sscanf.h>

/* Test: %s width limit splits the input across two conversions               */
MR_TEST(sscanf, fmt3_s_width_limit) {
    char first[8] = {0};
    char second[8] = {0};
    int ret;

    ret = mr_sscanf("abcdef", "%3s%4s", first, second);
    MR_TEST_ASSERT_EQ_INT(ret, 2);
    MR_TEST_ASSERT_EQ_INT(mr_strcmp(first, "abc"), 0);
    MR_TEST_ASSERT_EQ_INT(mr_strcmp(second, "def"), 0);
}

/* Test: %c with a width consumes exactly that many bytes and does not        */
/* terminate the buffer                                                       */
MR_TEST(sscanf, fmt3_c_multiple) {
    char first[4] = {0};
    char second[4] = {0};
    int ret;

    ret = mr_sscanf("abcd", "%2c%2c", first, second);
    MR_TEST_ASSERT_EQ_INT(ret, 2);
    MR_TEST_ASSERT_EQ_INT(mr_strncmp(first, "ab", 2), 0);
    MR_TEST_ASSERT_EQ_INT(mr_strncmp(second, "cd", 2), 0);
    MR_TEST_ASSERT_EQ_INT(first[2], '\0');
}

/* Test: a doubled percent in the format matches a literal percent            */
MR_TEST(sscanf, fmt3_percent_literal) {
    int value = -1;
    int ret;

    ret = mr_sscanf("42%", "%d%%", &value);
    MR_TEST_ASSERT_EQ_INT(ret, 1);
    MR_TEST_ASSERT_EQ_INT(value, 42);
}

#endif /* defined(MR_USE_SSCANF) */
#endif /* defined(MR_USE_TEST) */
