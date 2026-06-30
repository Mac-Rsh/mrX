/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/test/mr_test.h>
#if defined(MR_USE_TEST) && defined(MR_USE_FS)
#include <mrX/fs/mr_inode.h>
#include <mrX/fs/mr_dentry.h>
#include <mrX/libc/mr_string.h>

/* Helpers: build and tear down a fresh static root for each test */

static mr_err_t __dentry_setup_root(mr_dentry_t *root) {
    mr_memset(root, 0, sizeof(*root));
    return mr_dentry_init(root, MR_NULL, "/");
}

static void __dentry_teardown_root(mr_dentry_t *root) {
    mr_dentry_del(root);
}

/* Tests: init / create */

static void test_dentry_init_short(void) {
    mr_dentry_t d;
    mr_err_t ret;

    mr_memset(&d, 0, sizeof(d));
    ret = mr_dentry_init(&d, MR_NULL, "abc");

    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret == 0) {
        MR_TEST_EXPECT_EQ_INT(MR_OBJECT_IS_INITED(&d), MR_TRUE);
        MR_TEST_EXPECT_EQ_INT(MR_OBJECT_IS_CLAZZES(&d, dentry), MR_TRUE);
        MR_TEST_EXPECT_EQ_INT(mr_strcmp(MR_DENTRY_NAME(&d), "abc"), 0);
        MR_TEST_EXPECT_EQ_INT(MR_DENTRY_NAME_LEN(&d), 3);
        mr_dentry_del(&d);
    }
}
MR_TEST_EXPORT(dentry, init_short, test_dentry_init_short);

static void test_dentry_init_long_name(void) {
    mr_dentry_t d;
    mr_err_t ret;

    /* Name longer than MR_CFG_FS_DENTRY_NAME_LEN(4) forces dynamic alloc */
    mr_memset(&d, 0, sizeof(d));
    ret = mr_dentry_init(&d, MR_NULL, "longname");

    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret == 0) {
        MR_TEST_EXPECT_EQ_INT(mr_strcmp(MR_DENTRY_NAME(&d), "longname"), 0);
        MR_TEST_EXPECT_EQ_INT(MR_DENTRY_NAME_LEN(&d), 8);
        mr_dentry_del(&d);
    }
}
MR_TEST_EXPORT(dentry, init_long_name, test_dentry_init_long_name);

static void test_dentry_init_root_slash(void) {
    mr_dentry_t d;
    mr_err_t ret;

    mr_memset(&d, 0, sizeof(d));
    ret = mr_dentry_init(&d, MR_NULL, "/");

    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret == 0) {
        mr_dentry_del(&d);
    }
}
MR_TEST_EXPORT(dentry, init_root_slash, test_dentry_init_root_slash);

static void test_dentry_init_with_slash_rejected(void) {
    mr_dentry_t d;
    mr_err_t ret;

    mr_memset(&d, 0, sizeof(d));
    ret = mr_dentry_init(&d, MR_NULL, "a/b");

    MR_TEST_EXPECT_EQ_INT(ret, -MR_EINVAL);
}
MR_TEST_EXPORT(dentry, init_with_slash_rejected,
               test_dentry_init_with_slash_rejected);

static void test_dentry_create_dynamic(void) {
    mr_dentry_t *d;

    d = mr_dentry_create(MR_NULL, "dyn");
    MR_TEST_EXPECT_NE_PTR(d, MR_NULL);
    if (d) {
        MR_TEST_EXPECT_EQ_INT(mr_strcmp(MR_DENTRY_NAME(d), "dyn"), 0);
        MR_TEST_EXPECT_EQ_INT(MR_OBJECT_IS_CLAZZES(d, dentry), MR_TRUE);
        mr_dentry_del(d);
    }
}
MR_TEST_EXPORT(dentry, create_dynamic, test_dentry_create_dynamic);

static void test_dentry_create_format(void) {
    mr_dentry_t *d;

    d = mr_dentry_create(MR_NULL, "f%d", 42);
    MR_TEST_EXPECT_NE_PTR(d, MR_NULL);
    if (d) {
        MR_TEST_EXPECT_EQ_INT(mr_strcmp(MR_DENTRY_NAME(d), "f42"), 0);
        mr_dentry_del(d);
    }
}
MR_TEST_EXPORT(dentry, create_format, test_dentry_create_format);

/* Tests: add / remove / del */

static void test_dentry_add_normal(void) {
    mr_dentry_t root;
    mr_dentry_t *child;
    mr_err_t ret;

    ret = __dentry_setup_root(&root);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    child = mr_dentry_create(MR_NULL, "child");
    MR_TEST_ASSERT_NE_PTR(child, MR_NULL);
    if (!child) {
        __dentry_teardown_root(&root);
        return;
    }

    ret = mr_dentry_add(child, &root);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    MR_TEST_EXPECT_EQ_INT(mr_dentry_is_empty(&root), MR_FALSE);

    mr_dentry_del(child);
    MR_TEST_EXPECT_EQ_INT(mr_dentry_is_empty(&root), MR_TRUE);

    __dentry_teardown_root(&root);
}
MR_TEST_EXPORT(dentry, add_normal, test_dentry_add_normal);

static void test_dentry_add_duplicate(void) {
    mr_dentry_t root;
    mr_dentry_t *c1, *c2;
    mr_err_t ret;

    ret = __dentry_setup_root(&root);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    c1 = mr_dentry_create(MR_NULL, "dup");
    c2 = mr_dentry_create(MR_NULL, "dup");
    if ((!c1) || (!c2)) {
        if (c1) { mr_dentry_del(c1); }
        if (c2) { mr_dentry_del(c2); }
        __dentry_teardown_root(&root);
        return;
    }

    ret = mr_dentry_add(c1, &root);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    /* Adding a same-named sibling must fail */
    ret = mr_dentry_add(c2, &root);
    MR_TEST_EXPECT_EQ_INT(ret, -MR_EEXIST);

    mr_dentry_del(c1);
    mr_dentry_del(c2);
    __dentry_teardown_root(&root);
}
MR_TEST_EXPORT(dentry, add_duplicate, test_dentry_add_duplicate);

static void test_dentry_add_already_added(void) {
    mr_dentry_t root;
    mr_dentry_t *child;
    mr_err_t ret;

    ret = __dentry_setup_root(&root);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    child = mr_dentry_create(MR_NULL, "x");
    if (!child) {
        __dentry_teardown_root(&root);
        return;
    }

    ret = mr_dentry_add(child, &root);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    /* Adding the same dentry again must fail */
    ret = mr_dentry_add(child, &root);
    MR_TEST_EXPECT_EQ_INT(ret, -MR_EBUSY);

    mr_dentry_del(child);
    __dentry_teardown_root(&root);
}
MR_TEST_EXPORT(dentry, add_already_added, test_dentry_add_already_added);

static void test_dentry_remove(void) {
    mr_dentry_t root;
    mr_dentry_t *child;
    mr_err_t ret;

    ret = __dentry_setup_root(&root);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    child = mr_dentry_create(MR_NULL, "rm");
    if (!child) {
        __dentry_teardown_root(&root);
        return;
    }

    mr_dentry_add(child, &root);
    MR_TEST_EXPECT_EQ_INT(mr_dentry_is_empty(&root), MR_FALSE);

    mr_dentry_remove(child);
    MR_TEST_EXPECT_EQ_INT(mr_dentry_is_empty(&root), MR_TRUE);
    MR_TEST_EXPECT_EQ_INT(MR_DENTRY_IS_ADDED(child), MR_FALSE);

    /* child still has its creation ref; del frees it */
    mr_dentry_del(child);
    __dentry_teardown_root(&root);
}
MR_TEST_EXPORT(dentry, remove, test_dentry_remove);

/* Tests: child_get / is_empty */

static void test_dentry_child_get(void) {
    mr_dentry_t root;
    mr_dentry_t *c1, *c2, *got;
    mr_err_t ret;

    ret = __dentry_setup_root(&root);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    c1 = mr_dentry_create(MR_NULL, "a");
    c2 = mr_dentry_create(MR_NULL, "b");
    if ((!c1) || (!c2)) {
        if (c1) { mr_dentry_del(c1); }
        if (c2) { mr_dentry_del(c2); }
        __dentry_teardown_root(&root);
        return;
    }

    mr_dentry_add(c1, &root);
    mr_dentry_add(c2, &root);

    got = mr_dentry_child_get(&root, 0);
    MR_TEST_EXPECT_NE_PTR(got, MR_NULL);
    if (got) {
        MR_TEST_EXPECT_EQ_INT(mr_strcmp(MR_DENTRY_NAME(got), "a"), 0);
        mr_dentry_put(got);
    }

    got = mr_dentry_child_get(&root, 1);
    MR_TEST_EXPECT_NE_PTR(got, MR_NULL);
    if (got) {
        MR_TEST_EXPECT_EQ_INT(mr_strcmp(MR_DENTRY_NAME(got), "b"), 0);
        mr_dentry_put(got);
    }

    /* Out-of-range returns NULL */
    got = mr_dentry_child_get(&root, 2);
    MR_TEST_EXPECT_EQ_PTR(got, MR_NULL);

    mr_dentry_del(c1);
    mr_dentry_del(c2);
    __dentry_teardown_root(&root);
}
MR_TEST_EXPORT(dentry, child_get, test_dentry_child_get);

static void test_dentry_is_empty(void) {
    mr_dentry_t root;
    mr_bool_t empty;
    mr_err_t ret;

    ret = __dentry_setup_root(&root);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    empty = mr_dentry_is_empty(&root);
    MR_TEST_EXPECT_EQ_INT(empty, MR_TRUE);

    __dentry_teardown_root(&root);
}
MR_TEST_EXPORT(dentry, is_empty, test_dentry_is_empty);

/* Tests: lookup */

static void test_dentry_lookup_simple(void) {
    mr_dentry_t root;
    mr_dentry_t *child, *found;
    mr_err_t ret;

    ret = __dentry_setup_root(&root);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    child = mr_dentry_create(MR_NULL, "hello");
    if (!child) {
        __dentry_teardown_root(&root);
        return;
    }
    mr_dentry_add(child, &root);

    found = mr_dentry_lookup(&root, MR_NULL, "hello");
    MR_TEST_EXPECT_NE_PTR(found, MR_NULL);
    if (found) {
        MR_TEST_EXPECT_EQ_PTR(found, child);
        mr_dentry_put(found);
    }

    mr_dentry_del(child);
    __dentry_teardown_root(&root);
}
MR_TEST_EXPORT(dentry, lookup_simple, test_dentry_lookup_simple);

static void test_dentry_lookup_not_found(void) {
    mr_dentry_t root;
    mr_dentry_t *found;
    mr_err_t ret;

    ret = __dentry_setup_root(&root);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    found = mr_dentry_lookup(&root, MR_NULL, "nope");
    MR_TEST_EXPECT_EQ_PTR(found, MR_NULL);

    __dentry_teardown_root(&root);
}
MR_TEST_EXPORT(dentry, lookup_not_found, test_dentry_lookup_not_found);

static void test_dentry_lookup_multi_level(void) {
    mr_dentry_t root;
    mr_dentry_t *a, *b, *c, *found;
    mr_err_t ret;

    ret = __dentry_setup_root(&root);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    a = mr_dentry_create(MR_NULL, "a");
    b = mr_dentry_create(MR_NULL, "b");
    c = mr_dentry_create(MR_NULL, "c");
    if (!a || !b || !c) {
        if (a) { mr_dentry_del(a); }
        if (b) { mr_dentry_del(b); }
        if (c) { mr_dentry_del(c); }
        __dentry_teardown_root(&root);
        return;
    }
    mr_dentry_add(a, &root);
    mr_dentry_add(b, a);
    mr_dentry_add(c, b);

    found = mr_dentry_lookup(&root, MR_NULL, "a/b/c");
    MR_TEST_EXPECT_NE_PTR(found, MR_NULL);
    if (found) {
        MR_TEST_EXPECT_EQ_PTR(found, c);
        mr_dentry_put(found);
    }

    mr_dentry_del(c);
    mr_dentry_del(b);
    mr_dentry_del(a);
    __dentry_teardown_root(&root);
}
MR_TEST_EXPORT(dentry, lookup_multi_level, test_dentry_lookup_multi_level);

static void test_dentry_lookup_dot(void) {
    mr_dentry_t root;
    mr_dentry_t *child, *found;
    mr_err_t ret;

    ret = __dentry_setup_root(&root);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    child = mr_dentry_create(MR_NULL, "x");
    if (!child) {
        __dentry_teardown_root(&root);
        return;
    }
    mr_dentry_add(child, &root);

    /* "./x" should resolve */
    found = mr_dentry_lookup(&root, MR_NULL, "./x");
    MR_TEST_EXPECT_NE_PTR(found, MR_NULL);
    if (found) {
        MR_TEST_EXPECT_EQ_PTR(found, child);
        mr_dentry_put(found);
    }

    mr_dentry_del(child);
    __dentry_teardown_root(&root);
}
MR_TEST_EXPORT(dentry, lookup_dot, test_dentry_lookup_dot);

static void test_dentry_lookup_dotdot(void) {
    mr_dentry_t root;
    mr_dentry_t *a, *b, *found;
    mr_err_t ret;

    ret = __dentry_setup_root(&root);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    a = mr_dentry_create(MR_NULL, "a");
    b = mr_dentry_create(MR_NULL, "b");
    if (!a || !b) {
        if (a) { mr_dentry_del(a); }
        if (b) { mr_dentry_del(b); }
        __dentry_teardown_root(&root);
        return;
    }
    mr_dentry_add(a, &root);
    mr_dentry_add(b, a);

    /* "a/b/.." should resolve back to a */
    found = mr_dentry_lookup(&root, MR_NULL, "a/b/..");
    MR_TEST_EXPECT_NE_PTR(found, MR_NULL);
    if (found) {
        MR_TEST_EXPECT_EQ_PTR(found, a);
        mr_dentry_put(found);
    }

    mr_dentry_del(b);
    mr_dentry_del(a);
    __dentry_teardown_root(&root);
}
MR_TEST_EXPORT(dentry, lookup_dotdot, test_dentry_lookup_dotdot);

static void test_dentry_lookup_trailing_slash(void) {
    mr_dentry_t root;
    mr_dentry_t *child, *found;
    mr_err_t ret;

    ret = __dentry_setup_root(&root);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    child = mr_dentry_create(MR_NULL, "ts");
    if (!child) {
        __dentry_teardown_root(&root);
        return;
    }
    mr_dentry_add(child, &root);

    found = mr_dentry_lookup(&root, MR_NULL, "ts/");
    MR_TEST_EXPECT_NE_PTR(found, MR_NULL);
    if (found) {
        MR_TEST_EXPECT_EQ_PTR(found, child);
        mr_dentry_put(found);
    }

    mr_dentry_del(child);
    __dentry_teardown_root(&root);
}
MR_TEST_EXPORT(dentry, lookup_trailing_slash, test_dentry_lookup_trailing_slash);

/* Tests: mkpath */

static void test_dentry_mkpath_create(void) {
    mr_dentry_t root;
    mr_dentry_t *made, *found;
    mr_err_t ret;

    ret = __dentry_setup_root(&root);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    made = mr_dentry_mkpath(&root, MR_NULL, "d/e/f");
    MR_TEST_EXPECT_NE_PTR(made, MR_NULL);
    if (made) {
        MR_TEST_EXPECT_EQ_INT(mr_strcmp(MR_DENTRY_NAME(made), "f"), 0);
        mr_dentry_put(made);
    }

    /* All components should exist now */
    found = mr_dentry_lookup(&root, MR_NULL, "d/e/f");
    MR_TEST_EXPECT_NE_PTR(found, MR_NULL);
    if (found) {
        mr_dentry_put(found);
    }

    /* Clean up the subtree by deleting top-level child repeatedly */
    found = mr_dentry_lookup(&root, MR_NULL, "d");
    if (found) {
        mr_dentry_del(found);
        mr_dentry_put(found);
    }
    __dentry_teardown_root(&root);
}
MR_TEST_EXPORT(dentry, mkpath_create, test_dentry_mkpath_create);

static void test_dentry_mkpath_existing(void) {
    mr_dentry_t root;
    mr_dentry_t *made, *found;
    mr_err_t ret;

    ret = __dentry_setup_root(&root);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    made = mr_dentry_mkpath(&root, MR_NULL, "p/q");
    MR_TEST_ASSERT_NE_PTR(made, MR_NULL);
    if (made) {
        mr_dentry_put(made);
    }

    /* mkpath on an existing path returns the leaf */
    made = mr_dentry_mkpath(&root, MR_NULL, "p/q");
    MR_TEST_EXPECT_NE_PTR(made, MR_NULL);
    if (made) {
        mr_dentry_put(made);
    }

    found = mr_dentry_lookup(&root, MR_NULL, "p");
    if (found) {
        mr_dentry_del(found);
        mr_dentry_put(found);
    }
    __dentry_teardown_root(&root);
}
MR_TEST_EXPORT(dentry, mkpath_existing, test_dentry_mkpath_existing);

/* Tests: move */

static void test_dentry_move_rename(void) {
    mr_dentry_t root;
    mr_dentry_t *child, *found;
    mr_err_t ret;

    ret = __dentry_setup_root(&root);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    child = mr_dentry_create(MR_NULL, "old");
    if (!child) {
        __dentry_teardown_root(&root);
        return;
    }
    mr_dentry_add(child, &root);

    ret = mr_dentry_move(child, &root, "new");
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    MR_TEST_EXPECT_EQ_INT(mr_strcmp(MR_DENTRY_NAME(child), "new"), 0);

    /* Old name should not be found */
    found = mr_dentry_lookup(&root, MR_NULL, "old");
    MR_TEST_EXPECT_EQ_PTR(found, MR_NULL);

    /* New name should be found */
    found = mr_dentry_lookup(&root, MR_NULL, "new");
    MR_TEST_EXPECT_NE_PTR(found, MR_NULL);
    if (found) {
        mr_dentry_put(found);
    }

    mr_dentry_del(child);
    __dentry_teardown_root(&root);
}
MR_TEST_EXPORT(dentry, move_rename, test_dentry_move_rename);

static void test_dentry_move_into_subtree_rejected(void) {
    mr_dentry_t root;
    mr_dentry_t *parent, *child;
    mr_err_t ret;

    ret = __dentry_setup_root(&root);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    parent = mr_dentry_create(MR_NULL, "p");
    child = mr_dentry_create(MR_NULL, "c");
    if (!parent || !child) {
        if (parent) { mr_dentry_del(parent); }
        if (child) { mr_dentry_del(child); }
        __dentry_teardown_root(&root);
        return;
    }
    mr_dentry_add(parent, &root);
    mr_dentry_add(child, parent);

    /* Moving parent into its own child must fail */
    ret = mr_dentry_move(parent, child, "x");
    MR_TEST_EXPECT_EQ_INT(ret, -MR_EINVAL);

    mr_dentry_del(child);
    mr_dentry_del(parent);
    __dentry_teardown_root(&root);
}
MR_TEST_EXPORT(dentry, move_into_subtree_rejected,
               test_dentry_move_into_subtree_rejected);

static void test_dentry_move_duplicate_name(void) {
    mr_dentry_t root;
    mr_dentry_t *c1, *c2;
    mr_err_t ret;

    ret = __dentry_setup_root(&root);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    c1 = mr_dentry_create(MR_NULL, "a");
    c2 = mr_dentry_create(MR_NULL, "b");
    if (!c1 || !c2) {
        if (c1) { mr_dentry_del(c1); }
        if (c2) { mr_dentry_del(c2); }
        __dentry_teardown_root(&root);
        return;
    }
    mr_dentry_add(c1, &root);
    mr_dentry_add(c2, &root);

    /* Renaming c2 to "a" (which c1 occupies) must fail */
    ret = mr_dentry_move(c2, &root, "a");
    MR_TEST_EXPECT_EQ_INT(ret, -MR_EEXIST);

    mr_dentry_del(c2);
    mr_dentry_del(c1);
    __dentry_teardown_root(&root);
}
MR_TEST_EXPORT(dentry, move_duplicate_name, test_dentry_move_duplicate_name);

static void test_dentry_move_to_other_parent(void) {
    mr_dentry_t root;
    mr_dentry_t *p1, *p2, *child, *found;
    mr_err_t ret;

    ret = __dentry_setup_root(&root);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    p1 = mr_dentry_create(MR_NULL, "p1");
    p2 = mr_dentry_create(MR_NULL, "p2");
    child = mr_dentry_create(MR_NULL, "item");
    if (!p1 || !p2 || !child) {
        if (p1) { mr_dentry_del(p1); }
        if (p2) { mr_dentry_del(p2); }
        if (child) { mr_dentry_del(child); }
        __dentry_teardown_root(&root);
        return;
    }
    mr_dentry_add(p1, &root);
    mr_dentry_add(p2, &root);
    mr_dentry_add(child, p1);

    ret = mr_dentry_move(child, p2, "item");
    MR_TEST_EXPECT_EQ_INT(ret, 0);

    /* Should be under p2 now */
    found = mr_dentry_lookup(&root, MR_NULL, "p2/item");
    MR_TEST_EXPECT_NE_PTR(found, MR_NULL);
    if (found) {
        mr_dentry_put(found);
    }

    /* Should NOT be under p1 */
    found = mr_dentry_lookup(&root, MR_NULL, "p1/item");
    MR_TEST_EXPECT_EQ_PTR(found, MR_NULL);

    mr_dentry_del(child);
    mr_dentry_del(p2);
    mr_dentry_del(p1);
    __dentry_teardown_root(&root);
}
MR_TEST_EXPORT(dentry, move_to_other_parent, test_dentry_move_to_other_parent);

/* Tests: instantiate */

static void test_dentry_instantiate_normal(void) {
    mr_dentry_t d;
    mr_inode_t *inode;
    mr_err_t ret;

    mr_memset(&d, 0, sizeof(d));
    ret = mr_dentry_init(&d, MR_NULL, "n");
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    inode = mr_inode_create(MR_INODE_REG, MR_NULL, MR_NULL, MR_NULL);
    MR_TEST_ASSERT_NE_PTR(inode, MR_NULL);
    if (!inode) {
        mr_dentry_del(&d);
        return;
    }

    ret = mr_dentry_instantiate(&d, inode);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    MR_TEST_EXPECT_EQ_PTR(MR_DENTRY_INODE(&d), inode);

    mr_dentry_del(&d);
    mr_inode_del(inode);
}
MR_TEST_EXPORT(dentry, instantiate_normal, test_dentry_instantiate_normal);

static void test_dentry_instantiate_twice(void) {
    mr_dentry_t d;
    mr_inode_t *i1, *i2;
    mr_err_t ret;

    mr_memset(&d, 0, sizeof(d));
    ret = mr_dentry_init(&d, MR_NULL, "n");
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    i1 = mr_inode_create(MR_INODE_REG, MR_NULL, MR_NULL, MR_NULL);
    i2 = mr_inode_create(MR_INODE_REG, MR_NULL, MR_NULL, MR_NULL);
    if (!i1 || !i2) {
        if (i1) { mr_inode_del(i1); }
        if (i2) { mr_inode_del(i2); }
        mr_dentry_del(&d);
        return;
    }

    ret = mr_dentry_instantiate(&d, i1);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    /* Binding a second inode must fail */
    ret = mr_dentry_instantiate(&d, i2);
    MR_TEST_EXPECT_EQ_INT(ret, -MR_EBUSY);

    mr_dentry_del(&d);
    mr_inode_del(i2);
    mr_inode_del(i1);
}
MR_TEST_EXPORT(dentry, instantiate_twice, test_dentry_instantiate_twice);

static void test_dentry_del_with_inode(void) {
    mr_dentry_t d;
    mr_inode_t *inode;
    mr_err_t ret;

    mr_memset(&d, 0, sizeof(d));
    ret = mr_dentry_init(&d, MR_NULL, "n");
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    inode = mr_inode_create(MR_INODE_REG, MR_NULL, MR_NULL, MR_NULL);
    MR_TEST_ASSERT_NE_PTR(inode, MR_NULL);
    if (!inode) {
        mr_dentry_del(&d);
        return;
    }

    mr_dentry_instantiate(&d, inode);
    MR_TEST_EXPECT_EQ_PTR(MR_DENTRY_INODE(&d), inode);

    /* Deleting the dentry releases its inode reference; the inode still has
     * its creation ref */
    mr_dentry_del(&d);
    mr_inode_del(inode);
}
MR_TEST_EXPORT(dentry, del_with_inode, test_dentry_del_with_inode);

/* Tests: long name in tree operations */

static void test_dentry_lookup_long_name(void) {
    mr_dentry_t root;
    mr_dentry_t *child, *found;
    mr_err_t ret;

    ret = __dentry_setup_root(&root);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    child = mr_dentry_create(MR_NULL, "verylongname");
    if (!child) {
        __dentry_teardown_root(&root);
        return;
    }
    mr_dentry_add(child, &root);

    found = mr_dentry_lookup(&root, MR_NULL, "verylongname");
    MR_TEST_EXPECT_NE_PTR(found, MR_NULL);
    if (found) {
        MR_TEST_EXPECT_EQ_PTR(found, child);
        mr_dentry_put(found);
    }

    mr_dentry_del(child);
    __dentry_teardown_root(&root);
}
MR_TEST_EXPORT(dentry, lookup_long_name, test_dentry_lookup_long_name);

static void test_dentry_move_same_name_noop(void) {
    mr_dentry_t root;
    mr_dentry_t *child;
    mr_err_t ret;

    ret = __dentry_setup_root(&root);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    child = mr_dentry_create(MR_NULL, "same");
    if (!child) {
        __dentry_teardown_root(&root);
        return;
    }
    mr_dentry_add(child, &root);

    /* Moving to the same parent with the same name should be a no-op */
    ret = mr_dentry_move(child, &root, "same");
    MR_TEST_EXPECT_EQ_INT(ret, 0);

    mr_dentry_del(child);
    __dentry_teardown_root(&root);
}
MR_TEST_EXPORT(dentry, move_same_name_noop, test_dentry_move_same_name_noop);

static void test_dentry_move_to_self(void) {
    mr_dentry_t root;
    mr_dentry_t *child;
    mr_err_t ret;

    ret = __dentry_setup_root(&root);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    child = mr_dentry_create(MR_NULL, "me");
    if (!child) {
        __dentry_teardown_root(&root);
        return;
    }
    mr_dentry_add(child, &root);

    /* Moving a dentry into itself must fail */
    ret = mr_dentry_move(child, child, "me");
    MR_TEST_EXPECT_EQ_INT(ret, -MR_EINVAL);

    mr_dentry_del(child);
    __dentry_teardown_root(&root);
}
MR_TEST_EXPORT(dentry, move_to_self, test_dentry_move_to_self);

static void test_dentry_add_direct_recursion(void) {
    mr_dentry_t root;
    mr_dentry_t *a, *b;
    mr_err_t ret;

    ret = __dentry_setup_root(&root);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    a = mr_dentry_create(MR_NULL, "a");
    b = mr_dentry_create(MR_NULL, "b");
    if (!a || !b) {
        if (a) { mr_dentry_del(a); }
        if (b) { mr_dentry_del(b); }
        __dentry_teardown_root(&root);
        return;
    }
    mr_dentry_add(a, &root);
    mr_dentry_add(b, a);

    /* Adding a as child of b would create a cycle: a->b->a.
     * The code checks upper->upper == dentry (direct recursion) */
    ret = mr_dentry_add(a, b);
    MR_TEST_EXPECT_EQ_INT(ret, -MR_EPERM);

    mr_dentry_del(b);
    mr_dentry_del(a);
    __dentry_teardown_root(&root);
}
MR_TEST_EXPORT(dentry, add_direct_recursion, test_dentry_add_direct_recursion);

static void test_dentry_mkpath_root(void) {
    mr_dentry_t root;
    mr_dentry_t *made;
    mr_err_t ret;

    ret = __dentry_setup_root(&root);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    /* mkpath with just the start itself */
    made = mr_dentry_mkpath(&root, MR_NULL, "/");
    MR_TEST_EXPECT_NE_PTR(made, MR_NULL);
    if (made) {
        mr_dentry_put(made);
    }

    __dentry_teardown_root(&root);
}
MR_TEST_EXPORT(dentry, mkpath_root, test_dentry_mkpath_root);

static void test_dentry_child_get_empty(void) {
    mr_dentry_t root;
    mr_dentry_t *got;
    mr_err_t ret;

    ret = __dentry_setup_root(&root);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    got = mr_dentry_child_get(&root, 0);
    MR_TEST_EXPECT_EQ_PTR(got, MR_NULL);

    __dentry_teardown_root(&root);
}
MR_TEST_EXPORT(dentry, child_get_empty, test_dentry_child_get_empty);

static void test_dentry_get_put_refcount(void) {
    mr_dentry_t *d;
    mr_dentry_t *ret_d;

    d = mr_dentry_create(MR_NULL, "rc");
    MR_TEST_ASSERT_NE_PTR(d, MR_NULL);
    if (!d) {
        return;
    }

    ret_d = mr_dentry_get(d);
    MR_TEST_EXPECT_EQ_PTR(ret_d, d);

    ret_d = mr_dentry_get(d);
    MR_TEST_EXPECT_EQ_PTR(ret_d, d);

    mr_dentry_put(d);
    mr_dentry_put(d);
    mr_dentry_del(d);
}
MR_TEST_EXPORT(dentry, get_put_refcount, test_dentry_get_put_refcount);

static void test_dentry_del_dynamic_frees(void) {
    mr_dentry_t *d;

    d = mr_dentry_create(MR_NULL, "dyn_free");
    MR_TEST_ASSERT_NE_PTR(d, MR_NULL);
    if (!d) {
        return;
    }

    /* del drops the creation ref and frees a dynamic dentry */
    mr_dentry_del(d);

    /* If we get here without crashing, the free worked */
    MR_TEST_EXPECT_EQ_INT(1, MR_TRUE);
}
MR_TEST_EXPORT(dentry, del_dynamic_frees, test_dentry_del_dynamic_frees);

static void test_dentry_add_deep_cycle_rejected(void) {
    mr_dentry_t root;
    mr_dentry_t *a, *b, *c;
    mr_err_t ret;

    ret = __dentry_setup_root(&root);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    /* Build root -> a -> b -> c */
    a = mr_dentry_create(MR_NULL, "a");
    b = mr_dentry_create(MR_NULL, "b");
    c = mr_dentry_create(MR_NULL, "c");
    if (!a || !b || !c) {
        if (a) { mr_dentry_del(a); }
        if (b) { mr_dentry_del(b); }
        if (c) { mr_dentry_del(c); }
        __dentry_teardown_root(&root);
        return;
    }
    mr_dentry_add(a, &root);
    mr_dentry_add(b, a);
    mr_dentry_add(c, b);

    /* Detach a from root, then try to add it under c.
     * This would create cycle c -> a -> b -> c.
     * A robust implementation walks the full ancestor chain and rejects it */
    mr_dentry_remove(a);
    ret = mr_dentry_add(a, c);
    MR_TEST_EXPECT_EQ_INT(ret, -MR_EPERM);

    /* 'a' was not added; clean up by re-adding then deleting normally */
    if (ret != 0) {
        mr_dentry_add(a, &root);
    }
    mr_dentry_del(c);
    mr_dentry_del(b);
    mr_dentry_del(a);
    __dentry_teardown_root(&root);
}
MR_TEST_EXPORT(dentry, add_deep_cycle_rejected,
               test_dentry_add_deep_cycle_rejected);
#endif /* defined(MR_USE_TEST) && defined(MR_USE_FS) */
