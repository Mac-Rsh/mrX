/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/test/mr_test.h>
#if defined(MR_USE_TEST)
#include <mrX/kernel/mr_hook.h>

static void test_hook_entry(void) {
}

MR_TEST(hook, static_init) {
    mr_ptr_t buf[4];
    mr_hook_t hook = MR_HOOK_INIT(buf, 4);

    mr_hook_init(&hook, buf, 4);
    MR_TEST_ASSERT_EQ_INT(hook.size, 4);
    MR_TEST_ASSERT_EQ_INT(hook.next, 0);
}

MR_TEST(hook, init) {
    mr_ptr_t buf[4];
    mr_hook_t hook;

    mr_hook_init(&hook, buf, 4);
    MR_TEST_ASSERT_EQ_INT(hook.size, 4);
    MR_TEST_ASSERT_EQ_INT(hook.next, 0);
}

MR_TEST(hook, add) {
    mr_ptr_t buf[4];
    mr_hook_t hook;

    mr_hook_init(&hook, buf, 4);
    MR_TEST_ASSERT_EQ_INT(mr_hook_add(&hook, (mr_ptr_t)test_hook_entry), 0);
    MR_TEST_ASSERT_EQ_INT(hook.next, 1);
}

MR_TEST(hook, out_of_space) {
    mr_ptr_t buf[2];
    mr_hook_t hook;

    mr_hook_init(&hook, buf, 2);
    MR_TEST_ASSERT_EQ_INT(mr_hook_add(&hook, (mr_ptr_t)test_hook_entry), 0);
    MR_TEST_ASSERT_EQ_INT(mr_hook_add(&hook, (mr_ptr_t)test_hook_entry), 0);
    MR_TEST_ASSERT_EQ_INT(mr_hook_add(&hook, (mr_ptr_t)test_hook_entry),
                          -MR_ENOMEM);
}

typedef void(test_hook_fn_t)(void);

static int test_hook_sum;

static void test_hook_add1(void) {
    test_hook_sum += 1;
}

static void test_hook_add2(void) {
    test_hook_sum += 2;
}

MR_TEST(hook, each) {
    mr_ptr_t buf[4];
    mr_hook_t hook;
    test_hook_fn_t *entry;
    mr_uint32_t i;

    test_hook_sum = 0;
    mr_hook_init(&hook, buf, 4);
    MR_TEST_ASSERT_EQ_INT(mr_hook_add(&hook, (mr_ptr_t)test_hook_add1), 0);
    MR_TEST_ASSERT_EQ_INT(mr_hook_add(&hook, (mr_ptr_t)test_hook_add2), 0);
    MR_TEST_ASSERT_EQ_INT(mr_hook_add(&hook, (mr_ptr_t)test_hook_add1), 0);

    MR_HOOK_EACH(i, entry, test_hook_fn_t, &hook) {
        entry();
    }
    MR_TEST_ASSERT_EQ_INT(test_hook_sum, 4);
    MR_TEST_ASSERT_EQ_PTR(hook.entry[0], (mr_ptr_t)test_hook_add1);
    MR_TEST_ASSERT_EQ_PTR(hook.entry[1], (mr_ptr_t)test_hook_add2);
}

MR_TEST(hook, zero_size) {
    mr_hook_t hook;

    mr_hook_init(&hook, MR_NULL, 0);
    MR_TEST_ASSERT_EQ_INT(hook.size, 0);
    MR_TEST_ASSERT_EQ_INT(mr_hook_add(&hook, (mr_ptr_t)test_hook_entry),
                          -MR_ENOMEM);
}

#endif /* defined(MR_USE_TEST) */
