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

/* Backend rename callback recorder */
static mr_size_t __fsops_rename_calls;
static mr_bool_t __fsops_rename_fail;

static mr_err_t fsops_inode_rename(mr_inode_t *old_dir, mr_dentry_t *old_dentry,
                                   mr_inode_t *new_dir,
                                   mr_dentry_t *new_dentry) {
    MR_IGNORE(old_dir);
    MR_IGNORE(old_dentry);
    MR_IGNORE(new_dir);
    MR_IGNORE(new_dentry);

    __fsops_rename_calls++;
    return __fsops_rename_fail ? -MR_EIO : 0;
}

/* Backend unlink callback recorder */
static mr_size_t __fsops_unlink_calls;
static mr_bool_t __fsops_unlink_fail;
static const char *__fsops_unlink_name;

static mr_err_t fsops_inode_unlink(mr_inode_t *dir, const char *name) {
    MR_IGNORE(dir);

    __fsops_unlink_calls++;
    __fsops_unlink_name = name;
    return __fsops_unlink_fail ? -MR_EIO : 0;
}

static const mr_inode_ops_t __fsops_dir_ops = {
    .rename = fsops_inode_rename,
    .unlink = fsops_inode_unlink,
};

static void fsops_expect(volatile mr_uint32_t *__test_status,
                         const char *name, mr_int64_t actual,
                         mr_int64_t expected) {
    MR_IGNORE(name);

    MR_TEST_EXPECT_EQ_INT(actual, expected);
}

/* Test: dentry rename with backing directory inodes */
MR_TEST(fs, dentry_rename_api) {
    mr_dentry_t *root;
    mr_dentry_t *p1;
    mr_dentry_t *p2;
    mr_dentry_t *q1;
    mr_dentry_t *q2;
    mr_dentry_t *child;
    mr_dentry_t *child2;
    mr_dentry_t *childq;
    mr_inode_t *dir1;
    mr_inode_t *dir2;
    mr_inode_t *file;

    __fsops_rename_calls = 0;
    __fsops_rename_fail = MR_FALSE;

    /* Build two backed parents and two unbacked parents under a root */
    root = mr_dentry_create(MR_NULL, "%s", "root");
    fsops_expect(__test_status, "create root", root != MR_NULL, MR_TRUE);
    if (!root) {
        return;
    }
    dir1 = mr_inode_create(MR_INODE_DIR, &__fsops_dir_ops, MR_NULL, MR_NULL);
    dir2 = mr_inode_create(MR_INODE_DIR, &__fsops_dir_ops, MR_NULL, MR_NULL);
    fsops_expect(__test_status, "create dir inodes",
                 (dir1 != MR_NULL) && (dir2 != MR_NULL), MR_TRUE);
    if ((!dir1) || (!dir2)) {
        if (dir1) {
            mr_inode_put(dir1);
        }
        if (dir2) {
            mr_inode_put(dir2);
        }
        mr_dentry_del(root);
        return;
    }
    p1 = mr_dentry_create(MR_NULL, "%s", "p1");
    p2 = mr_dentry_create(MR_NULL, "%s", "p2");
    q1 = mr_dentry_create(MR_NULL, "%s", "q1");
    q2 = mr_dentry_create(MR_NULL, "%s", "q2");
    fsops_expect(__test_status, "create parents",
                 (p1 != MR_NULL) && (p2 != MR_NULL) && (q1 != MR_NULL)
                     && (q2 != MR_NULL), MR_TRUE);
    if ((!p1) || (!p2) || (!q1) || (!q2)) {
        if (p1) {
            mr_dentry_del(p1);
        }
        if (p2) {
            mr_dentry_del(p2);
        }
        if (q1) {
            mr_dentry_del(q1);
        }
        if (q2) {
            mr_dentry_del(q2);
        }
        mr_dentry_del(root);
        mr_inode_put(dir1);
        mr_inode_put(dir2);
        return;
    }
    fsops_expect(__test_status, "add p1", mr_dentry_add(p1, root), 0);
    fsops_expect(__test_status, "add p2", mr_dentry_add(p2, root), 0);
    fsops_expect(__test_status, "add q1", mr_dentry_add(q1, root), 0);
    fsops_expect(__test_status, "add q2", mr_dentry_add(q2, root), 0);
    fsops_expect(__test_status, "bind dir1", mr_dentry_instantiate(p1, dir1), 0);
    fsops_expect(__test_status, "bind dir2", mr_dentry_instantiate(p2, dir2), 0);

    child = mr_dentry_create(MR_NULL, "%s", "c");
    child2 = mr_dentry_create(MR_NULL, "%s", "c2");
    childq = mr_dentry_create(MR_NULL, "%s", "cq");
    fsops_expect(__test_status, "create children",
                 (child != MR_NULL) && (child2 != MR_NULL)
                     && (childq != MR_NULL), MR_TRUE);
    if ((!child) || (!child2) || (!childq)) {
        if (child) {
            mr_dentry_del(child);
        }
        if (child2) {
            mr_dentry_del(child2);
        }
        if (childq) {
            mr_dentry_del(childq);
        }
        mr_dentry_uninstantiate(p2);
        mr_dentry_uninstantiate(p1);
        mr_dentry_del(q2);
        mr_dentry_del(q1);
        mr_dentry_del(p2);
        mr_dentry_del(p1);
        mr_dentry_del(root);
        mr_inode_put(dir1);
        mr_inode_put(dir2);
        return;
    }
    file = mr_inode_create(MR_INODE_REG, MR_NULL, MR_NULL, MR_NULL);
    fsops_expect(__test_status, "create file inode", file != MR_NULL, MR_TRUE);
    if (file) {
        fsops_expect(__test_status, "bind file",
                     mr_dentry_instantiate(child, file), 0);
    }
    fsops_expect(__test_status, "add child", mr_dentry_add(child, p1), 0);
    fsops_expect(__test_status, "add child2", mr_dentry_add(child2, p1), 0);
    fsops_expect(__test_status, "add childq", mr_dentry_add(childq, q1), 0);

    /* An existing destination name is rejected before the backend callback */
    fsops_expect(__test_status, "rename onto child2",
                 mr_dentry_rename(child, p1, "c2"), -MR_EEXIST);
    fsops_expect(__test_status, "rename callback not called",
                 (mr_int64_t)__fsops_rename_calls, 0);

    /* A slash in the destination name is invalid */
    fsops_expect(__test_status, "rename slash name",
                 mr_dentry_rename(child, p2, "x/y"), -MR_EINVAL);

    /* Rename without backed parents is rejected and the tree is unchanged */
    fsops_expect(__test_status, "rename unbacked parents",
                 mr_dentry_rename(childq, q2, "moved"), -MR_ENOENT);
    fsops_expect(__test_status, "childq still in q1",
                 mr_dentry_is_empty(q1), MR_FALSE);

    /* A failing backend rename keeps the source in place */
    __fsops_rename_fail = MR_TRUE;
    fsops_expect(__test_status, "rename backend failure",
                 mr_dentry_rename(child, p2, "moved"), -MR_EIO);
    __fsops_rename_fail = MR_FALSE;
    fsops_expect(__test_status, "source parent keeps children",
                 mr_dentry_is_empty(p1), MR_FALSE);
    fsops_expect(__test_status, "destination parent empty",
                 mr_dentry_is_empty(p2), MR_TRUE);
    fsops_expect(__test_status, "failure reached backend",
                 (mr_int64_t)__fsops_rename_calls, 1);

    /* A successful backend rename moves and renames the dentry */
    fsops_expect(__test_status, "rename child",
                 mr_dentry_rename(child, p2, "moved"), 0);
    fsops_expect(__test_status, "rename callback called twice",
                 (mr_int64_t)__fsops_rename_calls, 2);
    fsops_expect(__test_status, "old parent keeps child2",
                 mr_dentry_is_empty(p1), MR_FALSE);
    fsops_expect(__test_status, "new parent has child",
                 mr_dentry_is_empty(p2), MR_FALSE);

    /* Teardown */
    fsops_expect(__test_status, "del child", mr_dentry_del(child), 0);
    fsops_expect(__test_status, "del child2", mr_dentry_del(child2), 0);
    fsops_expect(__test_status, "del childq", mr_dentry_del(childq), 0);
    if (file) {
        mr_inode_put(file);
    }
    fsops_expect(__test_status, "unbind dir2", mr_dentry_uninstantiate(p2), 0);
    fsops_expect(__test_status, "unbind dir1", mr_dentry_uninstantiate(p1), 0);
    fsops_expect(__test_status, "del q2", mr_dentry_del(q2), 0);
    fsops_expect(__test_status, "del q1", mr_dentry_del(q1), 0);
    fsops_expect(__test_status, "del p2", mr_dentry_del(p2), 0);
    fsops_expect(__test_status, "del p1", mr_dentry_del(p1), 0);
    fsops_expect(__test_status, "del root", mr_dentry_del(root), 0);
    mr_inode_put(dir1);
    mr_inode_put(dir2);
    return;
}

/* Test: dentry unlink with backing directory inodes */
MR_TEST(fs, dentry_unlink_api) {
    mr_dentry_t *root;
    mr_dentry_t *p;
    mr_dentry_t *p2;
    mr_dentry_t *pr;
    mr_dentry_t *ghost;
    mr_dentry_t *inreg;
    mr_dentry_t *sub;
    mr_dentry_t *leaf;
    mr_dentry_t *child;
    mr_inode_t *dir;
    mr_inode_t *dir2;
    mr_inode_t *reg;
    mr_inode_t *sub_dir;
    mr_inode_t *leaf_reg;
    mr_inode_t *data_reg;

    __fsops_unlink_calls = 0;
    __fsops_unlink_fail = MR_FALSE;
    __fsops_unlink_name = MR_NULL;

    /* Parent p carries an unlink callback, parent p2 does not */
    root = mr_dentry_create(MR_NULL, "%s", "root");
    fsops_expect(__test_status, "create root", root != MR_NULL, MR_TRUE);
    if (!root) {
        return;
    }
    dir = mr_inode_create(MR_INODE_DIR, &__fsops_dir_ops, MR_NULL, MR_NULL);
    dir2 = mr_inode_create(MR_INODE_DIR, MR_NULL, MR_NULL, MR_NULL);
    reg = mr_inode_create(MR_INODE_REG, MR_NULL, MR_NULL, MR_NULL);
    sub_dir = mr_inode_create(MR_INODE_DIR, MR_NULL, MR_NULL, MR_NULL);
    leaf_reg = mr_inode_create(MR_INODE_REG, MR_NULL, MR_NULL, MR_NULL);
    data_reg = mr_inode_create(MR_INODE_REG, MR_NULL, MR_NULL, MR_NULL);
    fsops_expect(__test_status, "create inodes",
                 (dir != MR_NULL) && (dir2 != MR_NULL) && (reg != MR_NULL)
                     && (sub_dir != MR_NULL) && (leaf_reg != MR_NULL)
                     && (data_reg != MR_NULL), MR_TRUE);
    p = mr_dentry_create(MR_NULL, "%s", "p");
    p2 = mr_dentry_create(MR_NULL, "%s", "p2");
    pr = mr_dentry_create(MR_NULL, "%s", "pr");
    fsops_expect(__test_status, "create parents",
                 (p != MR_NULL) && (p2 != MR_NULL) && (pr != MR_NULL), MR_TRUE);
    if ((!dir) || (!dir2) || (!reg) || (!sub_dir) || (!leaf_reg)
        || (!data_reg) || (!p) || (!p2) || (!pr)) {
        if (p) {
            mr_dentry_del(p);
        }
        if (p2) {
            mr_dentry_del(p2);
        }
        if (pr) {
            mr_dentry_del(pr);
        }
        mr_dentry_del(root);
        if (dir) {
            mr_inode_put(dir);
        }
        if (dir2) {
            mr_inode_put(dir2);
        }
        if (reg) {
            mr_inode_put(reg);
        }
        if (sub_dir) {
            mr_inode_put(sub_dir);
        }
        if (leaf_reg) {
            mr_inode_put(leaf_reg);
        }
        if (data_reg) {
            mr_inode_put(data_reg);
        }
        return;
    }
    fsops_expect(__test_status, "add p", mr_dentry_add(p, root), 0);
    fsops_expect(__test_status, "add p2", mr_dentry_add(p2, root), 0);
    fsops_expect(__test_status, "add pr", mr_dentry_add(pr, root), 0);
    fsops_expect(__test_status, "bind dir", mr_dentry_instantiate(p, dir), 0);
    fsops_expect(__test_status, "bind dir2", mr_dentry_instantiate(p2, dir2), 0);
    fsops_expect(__test_status, "bind reg parent",
                 mr_dentry_instantiate(pr, reg), 0);

    /* A child without an inode cannot be unlinked */
    ghost = mr_dentry_create(MR_NULL, "%s", "ghost");
    fsops_expect(__test_status, "create ghost", ghost != MR_NULL, MR_TRUE);
    if (ghost) {
        fsops_expect(__test_status, "add ghost", mr_dentry_add(ghost, p), 0);
        fsops_expect(__test_status, "unlink ghost", mr_dentry_unlink(ghost),
                     -MR_ENOENT);
        fsops_expect(__test_status, "del ghost", mr_dentry_del(ghost), 0);
    }

    /* A non-directory parent rejects unlink */
    inreg = mr_dentry_create(MR_NULL, "%s", "inreg");
    fsops_expect(__test_status, "create inreg", inreg != MR_NULL, MR_TRUE);
    if (inreg) {
        mr_inode_t *inreg_reg = mr_inode_create(MR_INODE_REG, MR_NULL, MR_NULL,
                                                MR_NULL);
        if (inreg_reg) {
            fsops_expect(__test_status, "bind inreg inode",
                         mr_dentry_instantiate(inreg, inreg_reg), 0);
        }
        fsops_expect(__test_status, "add inreg", mr_dentry_add(inreg, pr), 0);
        fsops_expect(__test_status, "unlink in non-dir parent",
                     mr_dentry_unlink(inreg), -MR_ENOTDIR);
        fsops_expect(__test_status, "del inreg", mr_dentry_del(inreg), 0);
        if (inreg_reg) {
            mr_inode_put(inreg_reg);
        }
    }

    /* A non-empty subdirectory cannot be unlinked without a callback */
    sub = mr_dentry_create(MR_NULL, "%s", "sub");
    leaf = mr_dentry_create(MR_NULL, "%s", "leaf");
    fsops_expect(__test_status, "create sub and leaf",
                 (sub != MR_NULL) && (leaf != MR_NULL), MR_TRUE);
    if ((!sub) || (!leaf)) {
        if (sub) {
            mr_dentry_del(sub);
        }
        if (leaf) {
            mr_dentry_del(leaf);
        }
    } else {
        fsops_expect(__test_status, "bind sub dir",
                     mr_dentry_instantiate(sub, sub_dir), 0);
        fsops_expect(__test_status, "bind leaf reg",
                     mr_dentry_instantiate(leaf, leaf_reg), 0);
        fsops_expect(__test_status, "add sub", mr_dentry_add(sub, p2), 0);
        fsops_expect(__test_status, "add leaf", mr_dentry_add(leaf, sub), 0);
        fsops_expect(__test_status, "unlink non-empty dir",
                     mr_dentry_unlink(sub), -MR_ENOTEMPTY);
        fsops_expect(__test_status, "unlink leaf", mr_dentry_unlink(leaf), 0);
        fsops_expect(__test_status, "unlink emptied dir",
                     mr_dentry_unlink(sub), 0);
        fsops_expect(__test_status, "sub parent empty", mr_dentry_is_empty(p2),
                     MR_TRUE);
    }

    /* A failing backend unlink keeps the entry attached */
    child = mr_dentry_create(MR_NULL, "%s", "data");
    fsops_expect(__test_status, "create data", child != MR_NULL, MR_TRUE);
    if (child) {
        fsops_expect(__test_status, "bind data reg",
                     mr_dentry_instantiate(child, data_reg), 0);
        fsops_expect(__test_status, "add data", mr_dentry_add(child, p), 0);
        __fsops_unlink_fail = MR_TRUE;
        fsops_expect(__test_status, "unlink backend failure",
                     mr_dentry_unlink(child), -MR_EIO);
        __fsops_unlink_fail = MR_FALSE;
        fsops_expect(__test_status, "failure reached backend",
                     (mr_int64_t)__fsops_unlink_calls, 1);
        if (__fsops_unlink_name) {
            fsops_expect(__test_status, "backend got the entry name",
                         mr_strcmp(__fsops_unlink_name, "data"), 0);
        }
        fsops_expect(__test_status, "entry still attached",
                     mr_dentry_is_empty(p), MR_FALSE);

        /* A successful unlink detaches and retires the entry */
        __fsops_unlink_calls = 0;
        fsops_expect(__test_status, "unlink data", mr_dentry_unlink(child), 0);
        fsops_expect(__test_status, "unlink reached backend",
                     (mr_int64_t)__fsops_unlink_calls, 1);
        fsops_expect(__test_status, "parent empty after unlink",
                     mr_dentry_is_empty(p), MR_TRUE);
    }

    /* Teardown. Unlinked entries release their own inode references */
    fsops_expect(__test_status, "unbind dir2", mr_dentry_uninstantiate(p2), 0);
    fsops_expect(__test_status, "unbind reg parent",
                 mr_dentry_uninstantiate(pr), 0);
    fsops_expect(__test_status, "unbind dir", mr_dentry_uninstantiate(p), 0);
    fsops_expect(__test_status, "del pr", mr_dentry_del(pr), 0);
    fsops_expect(__test_status, "del p2", mr_dentry_del(p2), 0);
    fsops_expect(__test_status, "del p", mr_dentry_del(p), 0);
    fsops_expect(__test_status, "del root", mr_dentry_del(root), 0);
    mr_inode_put(dir);
    mr_inode_put(dir2);
    mr_inode_put(reg);
    mr_inode_put(sub_dir);
    mr_inode_put(leaf_reg);
    mr_inode_put(data_reg);
    return;
}

#endif /* defined(MR_USE_TEST) && defined(MR_USE_FS) */
