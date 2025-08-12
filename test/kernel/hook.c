/**
 * @copyright (c) 2024-2025, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <test/mr_test.h>
#if defined(MR_USE_TEST)
#include <mr-X/mr_hook.h>

static void test_hook_entry(void) {
}

/* -------------------------------------------------------------------------- */
/* Test: Hook static init                                                     */
static void test_hook_static_init(void) {
    static mr_ptr_t buf[4];
    static mr_hook_t hook = MR_HOOK_INIT(buf, 4);

    mr_hook_init(&hook, buf, 4);
    MR_TEST_ASSERT_EQ_INT(hook.size, 4);
    MR_TEST_ASSERT_EQ_INT(hook.next, 0);
}
MR_TEST_EXPORT(hook, static_init, test_hook_static_init);

/* -------------------------------------------------------------------------- */
/* Test: Hook init                                                            */
static void test_hook_init(void) {
    mr_ptr_t buf[4];
    mr_hook_t hook;

    mr_hook_init(&hook, buf, 4);
    MR_TEST_ASSERT_EQ_INT(hook.size, 4);
    MR_TEST_ASSERT_EQ_INT(hook.next, 0);
}
MR_TEST_EXPORT(hook, init, test_hook_init);

/* -------------------------------------------------------------------------- */
/* Test: Hook add                                                             */
static void test_hook_add(void) {
    mr_ptr_t buf[4];
    mr_hook_t hook;

    mr_hook_init(&hook, buf, 4);
    MR_TEST_ASSERT_EQ_INT(mr_hook_add(&hook, (mr_ptr_t)test_hook_entry), 0);
    MR_TEST_ASSERT_EQ_INT(hook.next, 1);
}
MR_TEST_EXPORT(hook, add, test_hook_add);

/* -------------------------------------------------------------------------- */
/* Test: Hook out of space                                                    */
static void test_hook_out_of_space(void) {
    mr_ptr_t buf[2];
    mr_hook_t hook;

    mr_hook_init(&hook, buf, 2);
    MR_TEST_ASSERT_EQ_INT(mr_hook_add(&hook, (mr_ptr_t)test_hook_entry), 0);
    MR_TEST_ASSERT_EQ_INT(mr_hook_add(&hook, (mr_ptr_t)test_hook_entry), 0);
    MR_TEST_ASSERT_EQ_INT(mr_hook_add(&hook, (mr_ptr_t)test_hook_entry),
                          -MR_ENOMEM);
}
MR_TEST_EXPORT(hook, out_of_space, test_hook_out_of_space);
#endif /* defined(MR_USE_TEST) */
