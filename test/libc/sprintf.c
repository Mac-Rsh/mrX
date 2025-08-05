/**
 * @copyright (c) 2024-2025, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <test/mr_test.h>
#if defined(MR_USE_TEST)
#include <libc/mr_sprintf.h>
#include <libc/mr_string.h>

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
/* Test: snprintf with buffer size limit                                    */
static void test_snprintf_limit(void) {
    char buf[10];
    int ret;

    ret = mr_snprintf(buf, sizeof(buf), "%s", "1234567890");
    MR_TEST_ASSERT_EQ_INT(ret, 10);
    MR_TEST_ASSERT_EQ_INT(mr_strcmp(buf, "123456789"), 0);
}
MR_TEST_EXPORT(sprintf, snprintf_limit, test_snprintf_limit);

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
#endif /* defined(MR_USE_TEST) */
