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

/* Tests */
static void test_finder_init_del(void) {
    mr_finder_t finder;
    mr_err_t ret;

    ret = mr_finder_init(&finder, "finder");
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    MR_TEST_EXPECT_EQ_INT(MR_OBJECT_IS_INITED(&finder), MR_TRUE);
    MR_TEST_EXPECT_NE_PTR(MR_FINDER_NAME(&finder), MR_NULL);

    ret = mr_finder_del(&finder);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    MR_TEST_EXPECT_EQ_INT(MR_OBJECT_IS_INITED(&finder), MR_FALSE);
}
MR_TEST_EXPORT(finder, init_del, test_finder_init_del);

static void test_finder_add_remove(void) {
    mr_bool_t root_inited = MR_FALSE;
    mr_bool_t child_inited = MR_FALSE;
    mr_bool_t child_added = MR_FALSE;
    mr_finder_t child = {0};
    mr_finder_t root = {0};
    mr_finder_t *finder;
    mr_err_t ret;

    ret = mr_finder_init(&root, "root");
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret != 0) {
        goto _exit;
    }
    root_inited = MR_TRUE;

    ret = mr_finder_init(&child, "child");
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret != 0) {
        goto _exit;
    }
    child_inited = MR_TRUE;

    ret = mr_finder_add(&child, &root);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret != 0) {
        goto _exit;
    }
    child_added = MR_TRUE;
    MR_TEST_EXPECT_EQ_INT(MR_FINDER_IS_ADDED(&child), MR_TRUE);
    MR_TEST_EXPECT_EQ_PTR(child.upper, &root);

    finder = mr_finder_lookup(&root, MR_NULL, "child");
    MR_TEST_EXPECT_EQ_PTR(finder, &child);

    ret = mr_finder_remove(&child);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret == 0) {
        child_added = MR_FALSE;
    }
    MR_TEST_EXPECT_EQ_INT(MR_FINDER_IS_ADDED(&child), MR_FALSE);
    MR_TEST_EXPECT_EQ_PTR(child.upper, MR_NULL);

    ret = mr_finder_remove(&child);
    MR_TEST_EXPECT_EQ_INT(ret, 0);

    finder = mr_finder_lookup(&root, MR_NULL, "child");
    MR_TEST_EXPECT_EQ_PTR(finder, MR_NULL);

_exit:
    if (child_added == MR_TRUE) {
        ret = mr_finder_remove(&child);
        MR_TEST_EXPECT_EQ_INT(ret, 0);
    }
    if (child_inited == MR_TRUE) {
        ret = mr_finder_del(&child);
        MR_TEST_EXPECT_EQ_INT(ret, 0);
    }
    if (root_inited == MR_TRUE) {
        ret = mr_finder_del(&root);
        MR_TEST_EXPECT_EQ_INT(ret, 0);
    }
}
MR_TEST_EXPORT(finder, add_remove, test_finder_add_remove);

static void test_finder_create_del(void) {
    mr_finder_t *finder;

    finder = mr_finder_create("dynamic");
    MR_TEST_EXPECT_NE_PTR(finder, MR_NULL);
    if (finder == MR_NULL) {
        return;
    }

    MR_TEST_EXPECT_EQ_INT(MR_OBJECT_IS_INITED(finder), MR_TRUE);
    MR_TEST_EXPECT_NE_PTR(MR_FINDER_NAME(finder), MR_NULL);

    mr_finder_del(finder);
    MR_TEST_EXPECT_EQ_INT(MR_OBJECT_IS_INITED(finder), MR_FALSE);
}
MR_TEST_EXPORT(finder, create_del, test_finder_create_del);

static void test_finder_mkdir(void) {
    mr_finder_t root;
    mr_finder_t *found;
    mr_err_t ret;

    ret = mr_finder_init(&root, "root");
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    found = mr_finder_mkdir(&root, MR_NULL, "a/b/c");
    MR_TEST_EXPECT_NE_PTR(found, MR_NULL);
    if (found != MR_NULL) {
        MR_TEST_EXPECT_EQ_INT(MR_OBJECT_IS_INITED(found), MR_TRUE);
    }

    found = mr_finder_lookup(&root, MR_NULL, "a");
    MR_TEST_EXPECT_NE_PTR(found, MR_NULL);
    found = mr_finder_lookup(&root, MR_NULL, "a/b");
    MR_TEST_EXPECT_NE_PTR(found, MR_NULL);
    found = mr_finder_lookup(&root, MR_NULL, "a/b/c");
    MR_TEST_EXPECT_NE_PTR(found, MR_NULL);

    mr_finder_del(&root);
}
MR_TEST_EXPORT(finder, mkdir, test_finder_mkdir);

static void test_finder_mkdir_add(void) {
    mr_finder_t root;
    mr_finder_t child;
    mr_finder_t *found;
    mr_err_t ret;

    ret = mr_finder_init(&root, "root");
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    ret = mr_finder_init(&child, "myfile");
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret != 0) {
        mr_finder_del(&root);
        return;
    }

    ret = mr_finder_mkdir_add(&root, MR_NULL, &child, "dir1/dir2");
    MR_TEST_EXPECT_EQ_INT(ret, 0);

    found = mr_finder_lookup(&root, MR_NULL, "dir1/dir2/myfile");
    MR_TEST_EXPECT_EQ_PTR(found, &child);

    mr_finder_remove(&child);
    mr_finder_del(&child);
    mr_finder_del(&root);
}
MR_TEST_EXPORT(finder, mkdir_add, test_finder_mkdir_add);

static void test_finder_lookup_dot(void) {
    mr_finder_t root;
    mr_finder_t child;
    mr_finder_t *found;
    mr_err_t ret;

    ret = mr_finder_init(&root, "root");
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    ret = mr_finder_init(&child, "child");
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret != 0) {
        mr_finder_del(&root);
        return;
    }

    ret = mr_finder_add(&child, &root);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret != 0) {
        mr_finder_del(&child);
        mr_finder_del(&root);
        return;
    }

    found = mr_finder_lookup(&root, MR_NULL, ".");
    MR_TEST_EXPECT_EQ_PTR(found, &root);

    found = mr_finder_lookup(&child, MR_NULL, ".");
    MR_TEST_EXPECT_EQ_PTR(found, &child);

    found = mr_finder_lookup(&child, MR_NULL, "..");
    MR_TEST_EXPECT_EQ_PTR(found, &root);

    found = mr_finder_lookup(&root, MR_NULL, "..");
    MR_TEST_EXPECT_EQ_PTR(found, &root);

    mr_finder_remove(&child);
    mr_finder_del(&child);
    mr_finder_del(&root);
}
MR_TEST_EXPORT(finder, lookup_dot, test_finder_lookup_dot);

static void test_finder_lookup_limit(void) {
    mr_finder_t root;
    mr_finder_t child;
    mr_finder_t grandchild;
    mr_finder_t *found;
    mr_bool_t child_added = MR_FALSE;
    mr_bool_t grandchild_added = MR_FALSE;
    mr_err_t ret;

    ret = mr_finder_init(&root, "root");
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret != 0) { goto _exit; }

    ret = mr_finder_init(&child, "child");
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret != 0) { goto _exit; }

    ret = mr_finder_init(&grandchild, "gc");
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret != 0) { goto _exit; }

    ret = mr_finder_add(&child, &root);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret != 0) { goto _exit; }
    child_added = MR_TRUE;

    ret = mr_finder_add(&grandchild, &child);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret != 0) { goto _exit; }
    grandchild_added = MR_TRUE;

    found = mr_finder_lookup(&grandchild, &child, "..");
    MR_TEST_EXPECT_EQ_PTR(found, &child);

_exit:
    if (grandchild_added == MR_TRUE) { mr_finder_remove(&grandchild); }
    if (child_added == MR_TRUE) { mr_finder_remove(&child); }
    mr_finder_del(&grandchild);
    mr_finder_del(&child);
    mr_finder_del(&root);
}
MR_TEST_EXPORT(finder, lookup_limit, test_finder_lookup_limit);

static void test_finder_lookup_nonexist(void) {
    mr_finder_t root;
    mr_finder_t *found;
    mr_err_t ret;

    ret = mr_finder_init(&root, "root");
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret != 0) { return; }

    found = mr_finder_lookup(&root, MR_NULL, "nope");
    MR_TEST_EXPECT_EQ_PTR(found, MR_NULL);

    found = mr_finder_lookup(&root, MR_NULL, "a/b/c");
    MR_TEST_EXPECT_EQ_PTR(found, MR_NULL);

    mr_finder_del(&root);
}
MR_TEST_EXPORT(finder, lookup_nonexist, test_finder_lookup_nonexist);

static void test_finder_lookup_null_start(void) {
    mr_finder_t *found;

    found = mr_finder_lookup(MR_NULL, MR_NULL, "test");
    MR_TEST_EXPECT_EQ_PTR(found, MR_NULL);
}
MR_TEST_EXPORT(finder, lookup_null_start, test_finder_lookup_null_start);

static void test_finder_lookup_empty(void) {
    mr_finder_t root;
    mr_finder_t *found;
    mr_err_t ret;

    ret = mr_finder_init(&root, "root");
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret != 0) { return; }

    found = mr_finder_lookup(&root, MR_NULL, "");
    MR_TEST_EXPECT_EQ_PTR(found, MR_NULL);

    mr_finder_del(&root);
}
MR_TEST_EXPORT(finder, lookup_empty, test_finder_lookup_empty);

static void test_finder_add_dup(void) {
    mr_finder_t root;
    mr_finder_t child1, child2;
    mr_bool_t c1_added = MR_FALSE;
    mr_err_t ret;

    ret = mr_finder_init(&root, "root");
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret != 0) { return; }

    ret = mr_finder_init(&child1, "same");
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret != 0) { mr_finder_del(&root); return; }

    ret = mr_finder_init(&child2, "same");
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret != 0) { mr_finder_del(&child1); mr_finder_del(&root); return; }

    ret = mr_finder_add(&child1, &root);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret != 0) { goto _exit; }
    c1_added = MR_TRUE;

    ret = mr_finder_add(&child2, &root);
    MR_TEST_EXPECT_EQ_INT(ret, -MR_EEXIST);

_exit:
    if (c1_added == MR_TRUE) { mr_finder_remove(&child1); }
    mr_finder_del(&child1);
    mr_finder_del(&child2);
    mr_finder_del(&root);
}
MR_TEST_EXPORT(finder, add_dup, test_finder_add_dup);

static void test_finder_add_busy(void) {
    mr_finder_t root1, root2;
    mr_finder_t child;
    mr_bool_t child_added = MR_FALSE;
    mr_err_t ret;

    ret = mr_finder_init(&root1, "root1");
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret != 0) { return; }

    ret = mr_finder_init(&root2, "root2");
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret != 0) { mr_finder_del(&root1); return; }

    ret = mr_finder_init(&child, "child");
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret != 0) { mr_finder_del(&root2); mr_finder_del(&root1); return; }

    ret = mr_finder_add(&child, &root1);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret != 0) { goto _exit; }
    child_added = MR_TRUE;

    ret = mr_finder_add(&child, &root2);
    MR_TEST_EXPECT_EQ_INT(ret, -MR_EBUSY);

_exit:
    if (child_added == MR_TRUE) { mr_finder_remove(&child); }
    mr_finder_del(&child);
    mr_finder_del(&root2);
    mr_finder_del(&root1);
}
MR_TEST_EXPORT(finder, add_busy, test_finder_add_busy);

static void test_finder_add_recursion(void) {
    mr_finder_t root;
    mr_finder_t child;
    mr_bool_t child_added = MR_FALSE;
    mr_err_t ret;

    ret = mr_finder_init(&root, "root");
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret != 0) { return; }

    ret = mr_finder_init(&child, "child");
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret != 0) { mr_finder_del(&root); return; }

    ret = mr_finder_add(&child, &root);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret != 0) { goto _exit; }
    child_added = MR_TRUE;

    ret = mr_finder_add(&root, &child);
    MR_TEST_EXPECT_EQ_INT(ret, -MR_EPERM);

_exit:
    if (child_added == MR_TRUE) { mr_finder_remove(&child); }
    mr_finder_del(&child);
    mr_finder_del(&root);
}
MR_TEST_EXPORT(finder, add_recursion, test_finder_add_recursion);

static void test_finder_long_name(void) {
    mr_finder_t finder;
    mr_err_t ret;

    ret = mr_finder_init(&finder, "longname");
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret != 0) { return; }

    MR_TEST_EXPECT_EQ_UINT(MR_FINDER_NAME_LEN(&finder), 8);
    MR_TEST_EXPECT_NE_PTR(MR_FINDER_NAME(&finder), MR_NULL);

    mr_finder_del(&finder);
}
MR_TEST_EXPORT(finder, long_name, test_finder_long_name);

static void test_finder_name_slash(void) {
    mr_finder_t finder;
    mr_err_t ret;

    ret = mr_finder_init(&finder, "foo/bar");
    MR_TEST_EXPECT_EQ_INT(ret, -MR_EINVAL);
}
MR_TEST_EXPORT(finder, name_slash, test_finder_name_slash);
#endif /* defined(MR_USE_FINDER) */
#endif /* defined(MR_USE_TEST) */
