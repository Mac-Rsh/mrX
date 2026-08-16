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

static int dentry_ops_expect(const char *name, mr_int64_t actual,
                             mr_int64_t expected) {
    MR_IGNORE(name);

    MR_TEST_EXPECT_EQ_INT(actual, expected);
    return (actual == expected) ? 0 : 1;
}

static int fs_dentry_ops_test(void) {
    mr_dentry_t *root;
    mr_dentry_t *parent;
    mr_dentry_t *child;
    mr_dentry_t *other;
    mr_dentry_t *got;
    mr_inode_t *inode;
    int failures = 0;
    mr_err_t ret;

    /* Anchor root so parents can be added into a real tree */
    root = mr_dentry_create(MR_NULL, "%s", "root");
    failures += dentry_ops_expect("create root", root != MR_NULL, MR_TRUE);
    if (!root) {
        return failures;
    }

    /* Short inline name */
    parent = mr_dentry_create(MR_NULL, "%s", "p");
    failures += dentry_ops_expect("create parent", parent != MR_NULL, MR_TRUE);
    if (!parent) {
        mr_dentry_del(root);
        return failures;
    }
    failures += dentry_ops_expect("add parent",
                                  mr_dentry_add(parent, root), 0);

    child = mr_dentry_create(MR_NULL, "%s", "c");
    failures += dentry_ops_expect("create child", child != MR_NULL, MR_TRUE);
    if (!child) {
        mr_dentry_del(parent);
        mr_dentry_del(root);
        return failures;
    }

    /* Add child under parent */
    ret = mr_dentry_add(child, parent);
    failures += dentry_ops_expect("add child", ret, 0);

    /* Duplicate add is busy */
    failures += dentry_ops_expect("add again",
                                  mr_dentry_add(child, parent), -MR_EBUSY);

    /* Name clash */
    other = mr_dentry_create(MR_NULL, "%s", "c");
    failures += dentry_ops_expect("create clash", other != MR_NULL, MR_TRUE);
    if (other) {
        failures += dentry_ops_expect("add clash",
                                      mr_dentry_add(other, parent),
                                      -MR_EEXIST);
        mr_dentry_del(other);
    }

    /* Long name uses heap storage beyond the inline buffer */
    other = mr_dentry_create(MR_NULL, "%s", "longname");
    failures += dentry_ops_expect("create long name", other != MR_NULL,
                                  MR_TRUE);
    if (other) {
        failures += dentry_ops_expect("add long name",
                                      mr_dentry_add(other, parent), 0);
        failures += dentry_ops_expect("parent not empty",
                                      mr_dentry_is_empty(parent), MR_FALSE);

        /* child_get by index */
        got = mr_dentry_child_get(parent, 0);
        failures += dentry_ops_expect("child_get 0", got != MR_NULL, MR_TRUE);
        if (got) {
            mr_dentry_put(got);
        }
        got = mr_dentry_child_get(parent, 1);
        failures += dentry_ops_expect("child_get 1", got != MR_NULL, MR_TRUE);
        if (got) {
            mr_dentry_put(got);
        }
        got = mr_dentry_child_get(parent, 2);
        failures += dentry_ops_expect("child_get oor", got == MR_NULL,
                                      MR_TRUE);

        /* Cannot add parent under its own child (subtree cycle) */
        failures += dentry_ops_expect(
            "add parent into child",
            mr_dentry_add(parent, other), -MR_EPERM);

        /* Remove long name then delete */
        failures += dentry_ops_expect("remove long name",
                                      mr_dentry_remove(other), 0);
        failures += dentry_ops_expect("del long name",
                                      mr_dentry_del(other), 0);
    }

    /* Instantiate / uninstantiate */
    inode = mr_inode_create(MR_INODE_REG, MR_NULL, MR_NULL, MR_NULL);
    failures += dentry_ops_expect("create inode", inode != MR_NULL, MR_TRUE);
    if (inode) {
        failures += dentry_ops_expect("instantiate",
                                      mr_dentry_instantiate(child, inode), 0);
        failures += dentry_ops_expect(
            "instantiate twice", mr_dentry_instantiate(child, inode),
            -MR_EBUSY);
        failures += dentry_ops_expect("uninstantiate",
                                      mr_dentry_uninstantiate(child), 0);
        mr_inode_put(inode);
    }

    /* Move rename namespace-only to a sibling destination parent */
    other = mr_dentry_create(MR_NULL, "%s", "dst");
    failures += dentry_ops_expect("create dst parent", other != MR_NULL,
                                  MR_TRUE);
    if (other) {
        failures += dentry_ops_expect("add dst parent",
                                      mr_dentry_add(other, root), 0);
        failures += dentry_ops_expect("move child",
                                      mr_dentry_move(child, other, "moved"),
                                      0);
        failures += dentry_ops_expect("old parent empty",
                                      mr_dentry_is_empty(parent), MR_TRUE);
        failures += dentry_ops_expect("new parent not empty",
                                      mr_dentry_is_empty(other), MR_FALSE);
        failures += dentry_ops_expect(
            "move same name noop",
            mr_dentry_move(child, other, "moved"), 0);
        failures += dentry_ops_expect(
            "move slash name",
            mr_dentry_move(child, other, "has/slash"), -MR_EINVAL);
        got = mr_dentry_create(MR_NULL, "%s", "taken");
        failures += dentry_ops_expect("create taken", got != MR_NULL,
                                      MR_TRUE);
        if (got) {
            failures += dentry_ops_expect("add taken",
                                          mr_dentry_add(got, other), 0);
            failures += dentry_ops_expect(
                "move onto taken",
                mr_dentry_move(child, other, "taken"), -MR_EEXIST);
            failures += dentry_ops_expect("remove taken",
                                          mr_dentry_remove(got), 0);
            failures += dentry_ops_expect("del taken",
                                          mr_dentry_del(got), 0);
        }

        /* Move destination parent into its own child subtree */
        failures += dentry_ops_expect(
            "move into self subtree",
            mr_dentry_move(other, child, "loop"), -MR_EINVAL);

        failures += dentry_ops_expect("remove moved child",
                                      mr_dentry_remove(child), 0);
        failures += dentry_ops_expect("remove dst parent",
                                      mr_dentry_remove(other), 0);
        failures += dentry_ops_expect("del dst parent",
                                      mr_dentry_del(other), 0);
    }

    failures += dentry_ops_expect("del child", mr_dentry_del(child), 0);
    failures += dentry_ops_expect("remove parent",
                                  mr_dentry_remove(parent), 0);
    failures += dentry_ops_expect("del parent", mr_dentry_del(parent), 0);
    failures += dentry_ops_expect("del root", mr_dentry_del(root), 0);
    return failures;
}

static void dentry_ops(void) {
    MR_TEST_EXPECT_EQ_INT(fs_dentry_ops_test(), 0);
}
MR_TEST_EXPORT(fs, dentry_ops, dentry_ops);
#endif /* defined(MR_USE_TEST) && defined(MR_USE_FS) */
