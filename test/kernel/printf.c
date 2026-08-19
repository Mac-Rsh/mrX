/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2026-04-29        MacRsh        First version
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
MR_TEST(printf, null_fmt) {
    MR_TEST_ASSERT_EQ_INT(mr_printf(MR_NULL), -MR_EINVAL);
}

MR_TEST(printf, empty_fmt) {
    MR_TEST_ASSERT_EQ_INT(mr_printf(""), -MR_EINVAL);
}

MR_TEST(printf, basic) {
    int ret;

    ret = mr_printf("simple string\n");
    MR_TEST_ASSERT_GT_INT(ret, 0);

    ret = mr_printf("int: %d, hex: %x, str: %s\n", 42, 0xff, "hello");
    MR_TEST_ASSERT_GT_INT(ret, 0);
}

MR_TEST(printf, vprintf_null_fmt) {
    MR_TEST_ASSERT_EQ_INT(vprintf_wrapper(MR_NULL), -MR_EINVAL);
}

MR_TEST(printf, vprintf_empty_fmt) {
    MR_TEST_ASSERT_EQ_INT(vprintf_wrapper(""), -MR_EINVAL);
}

MR_TEST(printf, vprintf_basic) {
    int ret;

    ret = vprintf_wrapper("simple string\n");
    MR_TEST_ASSERT_GT_INT(ret, 0);

    ret = vprintf_wrapper("int: %d, hex: %x, str: %s\n", 42, 0xff, "hello");
    MR_TEST_ASSERT_GT_INT(ret, 0);

    ret = vprintf_wrapper("%x %04u %c\n", 0xab, 42, 'Z');
    MR_TEST_ASSERT_GT_INT(ret, 0);
}

MR_TEST(printf, vprintf_long_format) {
    int ret;

    ret = vprintf_wrapper(
        "this_is_a_very_long_format_string_that_exceeds_sixty_four_bytes_%s\n",
        "done");
    MR_TEST_ASSERT_GT_INT(ret, 0);
}

#endif /* defined(MR_USE_PRINTF) */
#endif /* defined(MR_USE_TEST) */
