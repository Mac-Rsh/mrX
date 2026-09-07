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

static mr_err_t sblock_ops_mount(mr_sblock_t *sb, const char *source, int flags,
                                 void *data) {
    MR_IGNORE(source);
    MR_IGNORE(flags);
    MR_IGNORE(data);

    sb->root = mr_inode_create(MR_INODE_DIR, MR_NULL, MR_NULL, MR_NULL);
    return sb->root ? 0 : -MR_ENOMEM;
}

static mr_file_system_t __sblock_ops_fs
    = MR_FILE_SYSTEM_INIT("sb-ops", sblock_ops_mount, MR_NULL);

static void sblock_ops_expect(volatile mr_uint32_t *__test_status,
                              const char *name, mr_int64_t actual,
                              mr_int64_t expected) {
    MR_IGNORE(name);

    MR_TEST_EXPECT_EQ_INT(actual, expected);
}

/* Test: attach, find, open lifetime and detach of a super-block */
MR_TEST(fs, sblock_attach_lifecycle) {
    mr_sblock_t *sb;
    mr_sblock_t *found;
    mr_dentry_t *d;
    mr_dentry_t *u;
    mr_dentry_t *wrong;
    mr_dentry_t *mnt;
    mr_dentry_t *root;
    mr_inode_t *reg;

    /* Create a super-block and an unrelated dentry pair */
    sb = MR_NULL;
    sblock_ops_expect(__test_status, "create super-block",
                      mr_sblock_create(&__sblock_ops_fs, MR_NULL, 0, MR_NULL,
                                       &sb), 0);
    sblock_ops_expect(__test_status, "super-block created", sb != MR_NULL,
                      MR_TRUE);
    if (!sb) {
        return;
    }
    u = mr_dentry_create(MR_NULL, "%s", "other");
    sblock_ops_expect(__test_status, "create other", u != MR_NULL, MR_TRUE);
    if (!u) {
        mr_sblock_del(sb);
        return;
    }

    /* Detaching before attach is rejected */
    mnt = MR_NULL;
    root = MR_NULL;
    sblock_ops_expect(__test_status, "detach before attach",
                      mr_sblock_detach(sb, &mnt, &root), -MR_ENOENT);

    /* Attach requires the root dentry to carry the super-block root inode */
    reg = mr_inode_create(MR_INODE_REG, MR_NULL, MR_NULL, MR_NULL);
    wrong = mr_dentry_create(MR_NULL, "%s", "wrong");
    sblock_ops_expect(__test_status, "create wrong pair",
                      (reg != MR_NULL) && (wrong != MR_NULL), MR_TRUE);
    if (reg && wrong) {
        sblock_ops_expect(__test_status, "bind wrong inode",
                          mr_dentry_instantiate(wrong, reg), 0);
        sblock_ops_expect(__test_status, "attach foreign root",
                          mr_sblock_attach(sb, wrong, wrong), -MR_EINVAL);
        sblock_ops_expect(__test_status, "unbind wrong inode",
                          mr_dentry_uninstantiate(wrong), 0);
    }
    if (reg) {
        mr_inode_put(reg);
    }
    if (wrong) {
        mr_dentry_del(wrong);
    }

    /* An unattached super-block is invisible to lookup by dentry */
    found = mr_sblock_find_by_dentry(u);
    sblock_ops_expect(__test_status, "find before attach", found == MR_NULL,
                      MR_TRUE);

    /* Attach the super-block to its root dentry in place */
    d = mr_dentry_create(MR_NULL, "%s", "mnt");
    sblock_ops_expect(__test_status, "create mnt", d != MR_NULL, MR_TRUE);
    if (!d) {
        mr_dentry_del(u);
        mr_sblock_del(sb);
        return;
    }
    sblock_ops_expect(__test_status, "bind root inode",
                      mr_dentry_instantiate(d, sb->root), 0);
    sblock_ops_expect(__test_status, "attach", mr_sblock_attach(sb, d, d), 0);

    /* A second attach is rejected while mounted */
    sblock_ops_expect(__test_status, "attach twice",
                      mr_sblock_attach(sb, d, d), -MR_EBUSY);

    /* The mounted super-block is found by its mount and root dentries */
    found = mr_sblock_find_by_dentry(d);
    sblock_ops_expect(__test_status, "find by mnt", found == sb, MR_TRUE);
    if (found) {
        mr_sblock_put(found);
    }

    /* Operations and open lifetimes are accepted while active */
    sblock_ops_expect(__test_status, "op enter", mr_sblock_op_enter(sb), 0);
    mr_sblock_op_exit(sb);
    sblock_ops_expect(__test_status, "file acquire", mr_sblock_file_acquire(sb),
                      0);

    /* An open file blocks detach until released */
    mnt = MR_NULL;
    root = MR_NULL;
    sblock_ops_expect(__test_status, "detach with open file",
                      mr_sblock_detach(sb, &mnt, &root), -MR_EBUSY);
    mr_sblock_file_release(sb);

    /* Detach transfers the dentry references to the caller */
    sblock_ops_expect(__test_status, "detach", mr_sblock_detach(sb, &mnt, &root),
                      0);
    sblock_ops_expect(__test_status, "detached mnt", mnt == d, MR_TRUE);
    sblock_ops_expect(__test_status, "detached root", root == d, MR_TRUE);
    if (mnt) {
        mr_dentry_put(mnt);
    }
    if (root) {
        mr_dentry_put(root);
    }

    /* A detached super-block rejects namespace lifetimes again */
    sblock_ops_expect(__test_status, "op enter after detach",
                      mr_sblock_op_enter(sb), -MR_ENOENT);
    sblock_ops_expect(__test_status, "file acquire after detach",
                      mr_sblock_file_acquire(sb), -MR_ENOENT);
    found = mr_sblock_find_by_dentry(d);
    sblock_ops_expect(__test_status, "find after detach", found == MR_NULL,
                      MR_TRUE);

    /* Teardown */
    sblock_ops_expect(__test_status, "del d", mr_dentry_del(d), 0);
    sblock_ops_expect(__test_status, "del u", mr_dentry_del(u), 0);
    sblock_ops_expect(__test_status, "del sb", mr_sblock_del(sb), 0);
    return;
}

/* Test: nested mounts, leaf lookup and subtree offline */
MR_TEST(fs, sblock_subtree_offline) {
    mr_sblock_t *parent;
    mr_sblock_t *child;
    mr_sblock_t *leaf;
    mr_dentry_t *d1;
    mr_dentry_t *d2;
    mr_dentry_t *d3;
    mr_dentry_t *mnt;
    mr_dentry_t *root;

    parent = MR_NULL;
    child = MR_NULL;
    sblock_ops_expect(__test_status, "create parent",
                      mr_sblock_create(&__sblock_ops_fs, MR_NULL, 0, MR_NULL,
                                       &parent), 0);
    sblock_ops_expect(__test_status, "create child",
                      mr_sblock_create(&__sblock_ops_fs, MR_NULL, 0, MR_NULL,
                                       &child), 0);
    sblock_ops_expect(__test_status, "super-blocks created",
                      (parent != MR_NULL) && (child != MR_NULL), MR_TRUE);
    if ((!parent) || (!child)) {
        if (parent) {
            mr_sblock_del(parent);
        }
        if (child) {
            mr_sblock_del(child);
        }
        return;
    }

    /* Attach the parent in place at its root dentry */
    d1 = mr_dentry_create(MR_NULL, "%s", "root");
    d3 = mr_dentry_create(MR_NULL, "%s", "child-root");
    sblock_ops_expect(__test_status, "create dentries",
                      (d1 != MR_NULL) && (d3 != MR_NULL), MR_TRUE);
    if ((!d1) || (!d3)) {
        if (d1) {
            mr_dentry_del(d1);
        }
        if (d3) {
            mr_dentry_del(d3);
        }
        mr_sblock_del(child);
        mr_sblock_del(parent);
        return;
    }
    sblock_ops_expect(__test_status, "bind parent root",
                      mr_dentry_instantiate(d1, parent->root), 0);
    sblock_ops_expect(__test_status, "bind child root",
                      mr_dentry_instantiate(d3, child->root), 0);
    sblock_ops_expect(__test_status, "attach parent",
                      mr_sblock_attach(parent, d1, d1), 0);

    /* Attach the child over a directory of the parent mount */
    d2 = mr_dentry_create(MR_NULL, "%s", "sub");
    sblock_ops_expect(__test_status, "create sub", d2 != MR_NULL, MR_TRUE);
    if (!d2) {
        mr_dentry_del(d3);
        mr_dentry_del(d1);
        mr_sblock_del(child);
        mr_sblock_del(parent);
        return;
    }
    sblock_ops_expect(__test_status, "bind sub inode",
                      mr_dentry_instantiate(d2, parent->root), 0);
    sblock_ops_expect(__test_status, "add sub", mr_dentry_add(d2, d1), 0);
    sblock_ops_expect(__test_status, "attach child",
                      mr_sblock_attach(child, d2, d3), 0);

    /* The leaf of the subtree is the deepest first child mount */
    leaf = mr_sblock_subtree_leaf_get(parent);
    sblock_ops_expect(__test_status, "parent leaf is child", leaf == child,
                      MR_TRUE);
    if (leaf) {
        mr_sblock_put(leaf);
    }
    leaf = mr_sblock_subtree_leaf_get(child);
    sblock_ops_expect(__test_status, "child leaf is child", leaf == child,
                      MR_TRUE);
    if (leaf) {
        mr_sblock_put(leaf);
    }

    /* The child mount accepts operations and an open file */
    sblock_ops_expect(__test_status, "child op enter",
                      mr_sblock_op_enter(child), 0);
    mr_sblock_op_exit(child);
    sblock_ops_expect(__test_status, "child file acquire",
                      mr_sblock_file_acquire(child), 0);

    /* Taking the parent subtree offline covers the child as well */
    mr_sblock_offline_subtree(parent);
    sblock_ops_expect(__test_status, "parent op enter offline",
                      mr_sblock_op_enter(parent), -MR_ENODEV);
    sblock_ops_expect(__test_status, "child op enter offline",
                      mr_sblock_op_enter(child), -MR_ENODEV);
    sblock_ops_expect(__test_status, "child file acquire offline",
                      mr_sblock_file_acquire(child), -MR_ENODEV);

    /* An offline mount detaches despite the open file and stays offline */
    mnt = MR_NULL;
    root = MR_NULL;
    sblock_ops_expect(__test_status, "detach offline child",
                      mr_sblock_detach(child, &mnt, &root), 0);
    if (mnt) {
        mr_dentry_put(mnt);
    }
    if (root) {
        mr_dentry_put(root);
    }
    mr_sblock_file_release(child);
    mnt = MR_NULL;
    root = MR_NULL;
    sblock_ops_expect(__test_status, "detach parent",
                      mr_sblock_detach(parent, &mnt, &root), 0);
    if (mnt) {
        mr_dentry_put(mnt);
    }
    if (root) {
        mr_dentry_put(root);
    }

    /* Teardown */
    sblock_ops_expect(__test_status, "del d3", mr_dentry_del(d3), 0);
    sblock_ops_expect(__test_status, "del d2", mr_dentry_del(d2), 0);
    sblock_ops_expect(__test_status, "del d1", mr_dentry_del(d1), 0);
    sblock_ops_expect(__test_status, "del child sb", mr_sblock_del(child), 0);
    sblock_ops_expect(__test_status, "del parent sb", mr_sblock_del(parent), 0);
    return;
}

#endif /* defined(MR_USE_TEST) && defined(MR_USE_FS) */
