/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/test/mr_test.h>
#if defined(MR_USE_TEST)
#if defined(MR_USE_FINDER)
#include <mrX/kernel/mr_finder.h>

/* -------------------------------------------------------------------------- */
/* Test: Finder init / del                                                    */
static void test_finder_init_del(void) {
    mr_finder_t finder;
    mr_err_t ret;

    ret = mr_finder_init(&finder, "finder");
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(MR_OBJECT_IS_INITED(&finder), MR_TRUE);
    MR_TEST_ASSERT_NE_PTR(MR_FINDER_NAME(&finder), MR_NULL);

    ret = mr_finder_del(&finder);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(MR_OBJECT_IS_INITED(&finder), MR_FALSE);
}
MR_TEST_EXPORT(finder, init_del, test_finder_init_del);

/* -------------------------------------------------------------------------- */
/* Test: Finder add / remove                                                  */
static void test_finder_add_remove(void) {
    mr_finder_t root;
    mr_finder_t child;
    mr_finder_t *finder;
    mr_err_t ret;

    ret = mr_finder_init(&root, "root");
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    ret = mr_finder_init(&child, "child");
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    ret = mr_finder_add(&child, &root);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(MR_FINDER_IS_ADDED(&child), MR_TRUE);
    MR_TEST_ASSERT_EQ_PTR(child.upper, &root);

    finder = mr_finder_lookup(&root, MR_NULL, "child");
    MR_TEST_ASSERT_EQ_PTR(finder, &child);

    ret = mr_finder_remove(&child);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(MR_FINDER_IS_ADDED(&child), MR_FALSE);
    MR_TEST_ASSERT_EQ_PTR(child.upper, MR_NULL);

    ret = mr_finder_remove(&child);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    finder = mr_finder_lookup(&root, MR_NULL, "child");
    MR_TEST_ASSERT_EQ_PTR(finder, MR_NULL);

    ret = mr_finder_del(&child);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    ret = mr_finder_del(&root);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
}
MR_TEST_EXPORT(finder, add_remove, test_finder_add_remove);
#endif /* defined(MR_USE_FINDER) */
#endif /* defined(MR_USE_TEST) */
