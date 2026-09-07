/**
 * @copyright (c) 2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2026-08-14        MacRsh        First version
 */

#include <mrX/test/mr_test.h>
#if defined(MR_USE_TEST) && defined(MR_USE_FS)
#include <mrX.h>

static void dentry_ops_expect(volatile mr_uint32_t *__test_status,
                              const char *name, mr_int64_t actual,
                              mr_int64_t expected) {
    MR_IGNORE(name);

    MR_TEST_EXPECT_EQ_INT(actual, expected);
}

MR_TEST(fs, dentry_ops) {
    mr_dentry_t *root;
    mr_dentry_t *parent;
    mr_dentry_t *child;
    mr_dentry_t *other;
    mr_dentry_t *got;
    mr_inode_t *inode;
    mr_err_t ret;

    /* Anchor root so parents can be added into a real tree */
    root = mr_dentry_create(MR_NULL, "%s", "root");
    dentry_ops_expect(__test_status, "create root", root != MR_NULL, MR_TRUE);
    if (!root) {
        return;
    }

    /* Short inline name */
    parent = mr_dentry_create(MR_NULL, "%s", "p");
    dentry_ops_expect(__test_status, "create parent", parent != MR_NULL,
                      MR_TRUE);
    if (!parent) {
        mr_dentry_del(root);
        return;
    }
    dentry_ops_expect(__test_status, "add parent", mr_dentry_add(parent, root),
                      0);

    child = mr_dentry_create(MR_NULL, "%s", "c");
    dentry_ops_expect(__test_status, "create child", child != MR_NULL, MR_TRUE);
    if (!child) {
        mr_dentry_del(parent);
        mr_dentry_del(root);
        return;
    }

    /* Add child under parent */
    ret = mr_dentry_add(child, parent);
    dentry_ops_expect(__test_status, "add child", ret, 0);

    /* Duplicate add is busy */
    dentry_ops_expect(__test_status, "add again", mr_dentry_add(child, parent),
                      -MR_EBUSY);

    /* Name clash */
    other = mr_dentry_create(MR_NULL, "%s", "c");
    dentry_ops_expect(__test_status, "create clash", other != MR_NULL, MR_TRUE);
    if (other) {
        dentry_ops_expect(__test_status, "add clash",
                          mr_dentry_add(other, parent), -MR_EEXIST);
        mr_dentry_del(other);
    }

    /* Long name uses heap storage beyond the inline buffer */
    other = mr_dentry_create(MR_NULL, "%s", "longname");
    dentry_ops_expect(__test_status, "create long name", other != MR_NULL,
                      MR_TRUE);
    if (other) {
        dentry_ops_expect(__test_status, "add long name",
                          mr_dentry_add(other, parent), 0);
        dentry_ops_expect(__test_status, "parent not empty",
                          mr_dentry_is_empty(parent), MR_FALSE);

        /* child_get by index */
        got = mr_dentry_child_get(parent, 0);
        dentry_ops_expect(__test_status, "child_get 0", got != MR_NULL,
                          MR_TRUE);
        if (got) {
            mr_dentry_put(got);
        }
        got = mr_dentry_child_get(parent, 1);
        dentry_ops_expect(__test_status, "child_get 1", got != MR_NULL,
                          MR_TRUE);
        if (got) {
            mr_dentry_put(got);
        }
        got = mr_dentry_child_get(parent, 2);
        dentry_ops_expect(__test_status, "child_get oor", got == MR_NULL,
                          MR_TRUE);

        /* Cannot add parent under its own child (subtree cycle) */
        dentry_ops_expect(__test_status, "add parent into child",
                          mr_dentry_add(parent, other), -MR_EPERM);

        /* Remove long name then delete */
        dentry_ops_expect(__test_status, "remove long name",
                          mr_dentry_remove(other), 0);
        dentry_ops_expect(__test_status, "del long name", mr_dentry_del(other),
                          0);
    }

    /* Instantiate / uninstantiate */
    inode = mr_inode_create(MR_INODE_REG, MR_NULL, MR_NULL, MR_NULL);
    dentry_ops_expect(__test_status, "create inode", inode != MR_NULL, MR_TRUE);
    if (inode) {
        dentry_ops_expect(__test_status, "instantiate",
                          mr_dentry_instantiate(child, inode), 0);
        dentry_ops_expect(__test_status, "instantiate twice",
                          mr_dentry_instantiate(child, inode), -MR_EBUSY);
        dentry_ops_expect(__test_status, "uninstantiate",
                          mr_dentry_uninstantiate(child), 0);
        mr_inode_put(inode);
    }

    /* Move rename namespace-only to a sibling destination parent */
    other = mr_dentry_create(MR_NULL, "%s", "dst");
    dentry_ops_expect(__test_status, "create dst parent", other != MR_NULL,
                      MR_TRUE);
    if (other) {
        dentry_ops_expect(__test_status, "add dst parent",
                          mr_dentry_add(other, root), 0);
        dentry_ops_expect(__test_status, "move child",
                          mr_dentry_move(child, other, "moved"), 0);
        dentry_ops_expect(__test_status, "old parent empty",
                          mr_dentry_is_empty(parent), MR_TRUE);
        dentry_ops_expect(__test_status, "new parent not empty",
                          mr_dentry_is_empty(other), MR_FALSE);
        dentry_ops_expect(__test_status, "move same name noop",
                          mr_dentry_move(child, other, "moved"), 0);
        dentry_ops_expect(__test_status, "move slash name",
                          mr_dentry_move(child, other, "has/slash"),
                          -MR_EINVAL);
        got = mr_dentry_create(MR_NULL, "%s", "taken");
        dentry_ops_expect(__test_status, "create taken", got != MR_NULL,
                          MR_TRUE);
        if (got) {
            dentry_ops_expect(__test_status, "add taken",
                              mr_dentry_add(got, other), 0);
            dentry_ops_expect(__test_status, "move onto taken",
                              mr_dentry_move(child, other, "taken"),
                              -MR_EEXIST);
            dentry_ops_expect(__test_status, "remove taken",
                              mr_dentry_remove(got), 0);
            dentry_ops_expect(__test_status, "del taken", mr_dentry_del(got),
                              0);
        }

        /* Move destination parent into its own child subtree */
        dentry_ops_expect(__test_status, "move into self subtree",
                          mr_dentry_move(other, child, "loop"), -MR_EINVAL);

        dentry_ops_expect(__test_status, "remove moved child",
                          mr_dentry_remove(child), 0);
        dentry_ops_expect(__test_status, "remove dst parent",
                          mr_dentry_remove(other), 0);
        dentry_ops_expect(__test_status, "del dst parent", mr_dentry_del(other),
                          0);
    }

    dentry_ops_expect(__test_status, "del child", mr_dentry_del(child), 0);
    dentry_ops_expect(__test_status, "remove parent", mr_dentry_remove(parent),
                      0);
    dentry_ops_expect(__test_status, "del parent", mr_dentry_del(parent), 0);
    dentry_ops_expect(__test_status, "del root", mr_dentry_del(root), 0);
    return;
}

#endif /* defined(MR_USE_TEST) && defined(MR_USE_FS) */
