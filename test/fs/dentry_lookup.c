/**
 * @copyright (c) 2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2026-09-05        MacRsh        First version
 */

#include <mrX/test/mr_test.h>
#if defined(MR_USE_TEST) && defined(MR_USE_FS)
#include <mrX.h>

static void dentry_lookup_expect(volatile mr_uint32_t *__test_status,
                                 const char *name, mr_int64_t actual,
                                 mr_int64_t expected) {
    MR_IGNORE(name);

    MR_TEST_EXPECT_EQ_INT(actual, expected);
}

/* Test: direct dentry path lookup over a manually built tree */
MR_TEST(fs, dentry_lookup_api) {
    mr_dentry_t *root;
    mr_dentry_t *a;
    mr_dentry_t *b;
    mr_dentry_t *leaf;
    mr_dentry_t *result;
    mr_inode_t *inode;

    /* Build root/a/b/leaf */
    root = mr_dentry_create(MR_NULL, "%s", "root");
    dentry_lookup_expect(__test_status, "create root", root != MR_NULL, MR_TRUE);
    if (!root) {
        return;
    }
    a = mr_dentry_create(MR_NULL, "%s", "a");
    dentry_lookup_expect(__test_status, "create a", a != MR_NULL, MR_TRUE);
    if (!a) {
        mr_dentry_del(root);
        return;
    }
    b = mr_dentry_create(MR_NULL, "%s", "b");
    leaf = mr_dentry_create(MR_NULL, "%s", "leaf");
    dentry_lookup_expect(__test_status, "create b and leaf",
                         (b != MR_NULL) && (leaf != MR_NULL), MR_TRUE);
    if ((!b) || (!leaf)) {
        mr_dentry_del(a);
        mr_dentry_del(root);
        return;
    }
    dentry_lookup_expect(__test_status, "add a", mr_dentry_add(a, root), 0);
    dentry_lookup_expect(__test_status, "add b", mr_dentry_add(b, a), 0);
    dentry_lookup_expect(__test_status, "add leaf",
                         mr_dentry_add(leaf, b), 0);

    /* A plain multi-component path resolves to the leaf */
    result = MR_NULL;
    dentry_lookup_expect(__test_status, "resolve a/b/leaf",
                         mr_dentry_lookup(root, MR_NULL, "a/b/leaf", &result),
                         0);
    dentry_lookup_expect(__test_status, "resolved leaf", result == leaf,
                         MR_TRUE);
    if (result) {
        mr_dentry_put(result);
    }

    /* Dot components are skipped and dot-dot climbs the tree */
    result = MR_NULL;
    dentry_lookup_expect(__test_status, "resolve dots",
                         mr_dentry_lookup(root, MR_NULL, "a/./b/../b/leaf",
                                          &result), 0);
    dentry_lookup_expect(__test_status, "dots resolved leaf", result == leaf,
                         MR_TRUE);
    if (result) {
        mr_dentry_put(result);
    }

    /* Leading, trailing and repeated slashes resolve to the start or leaf */
    result = MR_NULL;
    dentry_lookup_expect(__test_status, "resolve slash path",
                         mr_dentry_lookup(root, MR_NULL, "/a/b/", &result), 0);
    dentry_lookup_expect(__test_status, "slash resolved b", result == b,
                         MR_TRUE);
    if (result) {
        mr_dentry_put(result);
    }
    result = MR_NULL;
    dentry_lookup_expect(__test_status, "resolve bare slashes",
                         mr_dentry_lookup(root, MR_NULL, "//", &result), 0);
    dentry_lookup_expect(__test_status, "slashes resolved root", result == root,
                         MR_TRUE);
    if (result) {
        mr_dentry_put(result);
    }

    /* The limit clamps dot-dot at the given dentry */
    result = MR_NULL;
    dentry_lookup_expect(__test_status, "resolve clamped dot-dot",
                         mr_dentry_lookup(root, b, "a/b/..", &result), 0);
    dentry_lookup_expect(__test_status, "dot-dot stopped at limit",
                         result == b, MR_TRUE);
    if (result) {
        mr_dentry_put(result);
    }
    result = MR_NULL;
    dentry_lookup_expect(__test_status, "resolve unclamped dot-dot",
                         mr_dentry_lookup(root, MR_NULL, "a/b/..", &result), 0);
    dentry_lookup_expect(__test_status, "dot-dot reached parent", result == a,
                         MR_TRUE);
    if (result) {
        mr_dentry_put(result);
    }

    /* A missing component on an unbacked tree reports not found */
    result = MR_NULL;
    dentry_lookup_expect(__test_status, "resolve missing leaf",
                         mr_dentry_lookup(root, MR_NULL, "a/missing", &result),
                         -MR_ENOENT);
    dentry_lookup_expect(__test_status, "missing leaf result", result == MR_NULL,
                         MR_TRUE);

    /* Walking through a non-directory inode is rejected */
    inode = mr_inode_create(MR_INODE_REG, MR_NULL, MR_NULL, MR_NULL);
    dentry_lookup_expect(__test_status, "create inode", inode != MR_NULL,
                         MR_TRUE);
    if (inode) {
        dentry_lookup_expect(__test_status, "instantiate leaf",
                             mr_dentry_instantiate(leaf, inode), 0);
        result = MR_NULL;
        dentry_lookup_expect(__test_status, "resolve through file",
                             mr_dentry_lookup(root, MR_NULL, "a/b/leaf/x",
                                              &result), -MR_ENOTDIR);
        dentry_lookup_expect(__test_status, "uninstantiate leaf",
                             mr_dentry_uninstantiate(leaf), 0);
        mr_inode_put(inode);
    }

    /* Teardown */
    dentry_lookup_expect(__test_status, "del leaf", mr_dentry_del(leaf), 0);
    dentry_lookup_expect(__test_status, "del b", mr_dentry_del(b), 0);
    dentry_lookup_expect(__test_status, "del a", mr_dentry_del(a), 0);
    dentry_lookup_expect(__test_status, "del root", mr_dentry_del(root), 0);
    return;
}

#endif /* defined(MR_USE_TEST) && defined(MR_USE_FS) */
