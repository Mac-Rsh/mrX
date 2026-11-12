/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2026-09-05        MacRsh        First version
 */

#include <mrX/test/mr_test.h>
#if defined(MR_USE_TEST)
#include <mrX/kernel/mr_initcall.h>

/* Initcall execution markers(filled by init calls before tests run) */
static volatile int initcall_kernel_ran;
static volatile int initcall_user_ran;
static volatile int initcall_kernel_before_user;
static volatile int initcall_low_before_high;

static void initcall_test_kernel(void) {
    initcall_kernel_ran = 1;
}
MR_INIT_EXPORT(initcall_test_kernel, MR_INIT_LEVEL_IRQ);

static void initcall_test_user_low(void) {
    initcall_user_ran = 1;
    if (initcall_kernel_ran == 1) {
        initcall_kernel_before_user = 1;
    }
}
MR_INIT_EXPORT(initcall_test_user_low, MR_INIT_LEVEL_USER("0"));

static void initcall_test_user_high(void) {
    if (initcall_user_ran == 1) {
        initcall_low_before_high = 1;
    }
}
MR_INIT_EXPORT(initcall_test_user_high, MR_INIT_LEVEL_USER("1"));

MR_TEST(initcall, kernel_level_ran) {
    MR_TEST_ASSERT_EQ_INT(initcall_kernel_ran, 1);
}

MR_TEST(initcall, user_level_ran) {
    MR_TEST_ASSERT_EQ_INT(initcall_user_ran, 1);
}

MR_TEST(initcall, level_order) {
    MR_TEST_ASSERT_EQ_INT(initcall_kernel_before_user, 1);
    MR_TEST_ASSERT_EQ_INT(initcall_low_before_high, 1);
}

MR_TEST(initcall, kernel_init_idempotent) {
    mr_kernel_init();
    MR_TEST_ASSERT_EQ_INT(initcall_kernel_ran, 1);
}

MR_TEST(initcall, user_init_idempotent) {
    mr_user_init();
    MR_TEST_ASSERT_EQ_INT(initcall_user_ran, 1);
    MR_TEST_ASSERT_EQ_INT(initcall_low_before_high, 1);
}

#endif /* defined(MR_USE_TEST) */
