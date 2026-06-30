/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/test/mr_test.h>
#if defined(MR_USE_TEST) && defined(MR_USE_FS)
#include <mrX/fs/mr_sb.h>
#include <mrX/fs/mr_fs.h>
#include <mrX/fs/mr_dentry.h>
#include <mrX/libc/mr_string.h>
#include "testfs.h"

/* Helpers: create a local file-system backed by the testfs callbacks */
static mr_err_t __sb_setup_fs(mr_file_system_t *fs, const char *name,
                              mr_file_system_mount_t *mount,
                              mr_file_system_kill_sb_t *kill_sb) {
    mr_memset(fs, 0, sizeof(*fs));
    return mr_file_system_init(fs, name, mount, kill_sb);
}

/* Fail-mount callback for failure-path tests */
static mr_err_t __sb_fail_mount(mr_super_block_t *sb, const char *source,
                                 int flags, void *data) {
    MR_IGNORE(sb);
    MR_IGNORE(source);
    MR_IGNORE(flags);
    MR_IGNORE(data);
    return -MR_EIO;
}

static void __sb_fail_kill(mr_super_block_t *sb) {
    MR_IGNORE(sb);
}

/* Bad-root mount: sets root to a regular file instead of a directory */
static mr_err_t __sb_bad_root_mount(mr_super_block_t *sb, const char *source,
                                     int flags, void *data) {
    mr_inode_t *inode;
    MR_IGNORE(source);
    MR_IGNORE(flags);
    MR_IGNORE(data);
    inode = mr_inode_create(MR_INODE_REG, MR_NULL, MR_NULL, MR_NULL);
    if (!inode) {
        return -MR_ENOMEM;
    }
    sb->root = inode;
    return 0;
}

static void __sb_bad_root_kill(mr_super_block_t *sb) {
    if (sb->root) {
        mr_inode_del(sb->root);
        sb->root = MR_NULL;
    }
}

/* Tests */

static void test_sb_create_normal(void) {
    mr_file_system_t fs;
    mr_super_block_t *sb;
    mr_err_t ret;

    ret = __sb_setup_fs(&fs, "sb_n", testfs_mount, testfs_kill_sb);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    sb = mr_super_block_create(&fs, MR_NULL, 0, MR_NULL);
    MR_TEST_EXPECT_NE_PTR(sb, MR_NULL);
    if (sb) {
        MR_TEST_EXPECT_EQ_INT(MR_OBJECT_IS_INITED(sb), MR_TRUE);
        MR_TEST_EXPECT_EQ_INT(MR_OBJECT_IS_CLAZZES(sb, superblock), MR_TRUE);
        MR_TEST_EXPECT_NE_PTR(sb->root, MR_NULL);
        MR_TEST_EXPECT_EQ_INT(sb->root->type, MR_INODE_DIR);
        MR_TEST_EXPECT_EQ_PTR(sb->root->sb, sb);

        mr_super_block_del(sb);
    }

    mr_file_system_del(&fs);
}
MR_TEST_EXPORT(sb, create_normal, test_sb_create_normal);

static void test_sb_create_mount_fail(void) {
    mr_file_system_t fs;
    mr_super_block_t *sb;
    mr_err_t ret;

    ret = __sb_setup_fs(&fs, "sb_f", __sb_fail_mount, __sb_fail_kill);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    sb = mr_super_block_create(&fs, MR_NULL, 0, MR_NULL);
    MR_TEST_EXPECT_EQ_PTR(sb, MR_NULL);

    mr_file_system_del(&fs);
}
MR_TEST_EXPORT(sb, create_mount_fail, test_sb_create_mount_fail);

static void test_sb_create_bad_root_type(void) {
    mr_file_system_t fs;
    mr_super_block_t *sb;
    mr_err_t ret;

    ret = __sb_setup_fs(&fs, "sb_bt", __sb_bad_root_mount, __sb_bad_root_kill);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    /* Root must be a directory; a regular-file root is rejected */
    sb = mr_super_block_create(&fs, MR_NULL, 0, MR_NULL);
    MR_TEST_EXPECT_EQ_PTR(sb, MR_NULL);

    mr_file_system_del(&fs);
}
MR_TEST_EXPORT(sb, create_bad_root_type, test_sb_create_bad_root_type);

static void test_sb_del_normal(void) {
    mr_file_system_t fs;
    mr_super_block_t *sb;
    mr_err_t ret;

    ret = __sb_setup_fs(&fs, "sb_d", testfs_mount, testfs_kill_sb);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    sb = mr_super_block_create(&fs, MR_NULL, 0, MR_NULL);
    MR_TEST_ASSERT_NE_PTR(sb, MR_NULL);
    if (!sb) {
        mr_file_system_del(&fs);
        return;
    }

    ret = mr_super_block_del(sb);
    MR_TEST_EXPECT_EQ_INT(ret, 0);

    mr_file_system_del(&fs);
}
MR_TEST_EXPORT(sb, del_normal, test_sb_del_normal);

static void test_sb_get_put_refcount(void) {
    mr_file_system_t fs;
    mr_super_block_t *sb, *ret_sb;
    mr_err_t ret;

    ret = __sb_setup_fs(&fs, "sb_r", testfs_mount, testfs_kill_sb);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    sb = mr_super_block_create(&fs, MR_NULL, 0, MR_NULL);
    MR_TEST_ASSERT_NE_PTR(sb, MR_NULL);
    if (!sb) {
        mr_file_system_del(&fs);
        return;
    }

    ret_sb = mr_super_block_get(sb);
    MR_TEST_EXPECT_EQ_PTR(ret_sb, sb);

    ret_sb = mr_super_block_get(sb);
    MR_TEST_EXPECT_EQ_PTR(ret_sb, sb);

    mr_super_block_put(sb);
    mr_super_block_put(sb);

    /* The create ref + sb list ref are still held; del cleans up */
    mr_super_block_del(sb);

    mr_file_system_del(&fs);
}
MR_TEST_EXPORT(sb, get_put_refcount, test_sb_get_put_refcount);

static void test_sb_find_by_mnt(void) {
    mr_file_system_t fs;
    mr_super_block_t *sb, *found;
    mr_dentry_t mnt;
    mr_err_t ret;

    ret = __sb_setup_fs(&fs, "sb_fb", testfs_mount, testfs_kill_sb);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    sb = mr_super_block_create(&fs, MR_NULL, 0, MR_NULL);
    MR_TEST_ASSERT_NE_PTR(sb, MR_NULL);
    if (!sb) {
        mr_file_system_del(&fs);
        return;
    }

    /* Create a dentry to use as the mount-point */
    mr_memset(&mnt, 0, sizeof(mnt));
    ret = mr_dentry_init(&mnt, MR_NULL, "mnt");
    if (ret != 0) {
        mr_super_block_del(sb);
        mr_file_system_del(&fs);
        return;
    }

    sb->mnt = &mnt;

    found = mr_super_block_find_by_mnt(&mnt);
    MR_TEST_EXPECT_NE_PTR(found, MR_NULL);
    if (found) {
        MR_TEST_EXPECT_EQ_PTR(found, sb);
        mr_super_block_put(found);
    }

    /* Detach and verify find fails */
    mr_super_block_detach_mnt(sb);
    found = mr_super_block_find_by_mnt(&mnt);
    MR_TEST_EXPECT_EQ_PTR(found, MR_NULL);

    mr_dentry_del(&mnt);
    mr_super_block_del(sb);
    mr_file_system_del(&fs);
}
MR_TEST_EXPORT(sb, find_by_mnt, test_sb_find_by_mnt);

static void test_sb_find_by_mnt_no_match(void) {
    mr_dentry_t mnt;
    mr_super_block_t *found;
    mr_err_t ret;

    mr_memset(&mnt, 0, sizeof(mnt));
    ret = mr_dentry_init(&mnt, MR_NULL, "nm");
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    found = mr_super_block_find_by_mnt(&mnt);
    MR_TEST_EXPECT_EQ_PTR(found, MR_NULL);

    mr_dentry_del(&mnt);
}
MR_TEST_EXPORT(sb, find_by_mnt_no_match, test_sb_find_by_mnt_no_match);

static void test_sb_detach_mnt(void) {
    mr_file_system_t fs;
    mr_super_block_t *sb;
    mr_dentry_t mnt;
    mr_err_t ret;

    ret = __sb_setup_fs(&fs, "sb_dm", testfs_mount, testfs_kill_sb);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    sb = mr_super_block_create(&fs, MR_NULL, 0, MR_NULL);
    MR_TEST_ASSERT_NE_PTR(sb, MR_NULL);
    if (!sb) {
        mr_file_system_del(&fs);
        return;
    }

    mr_memset(&mnt, 0, sizeof(mnt));
    ret = mr_dentry_init(&mnt, MR_NULL, "dm");
    if (ret != 0) {
        mr_super_block_del(sb);
        mr_file_system_del(&fs);
        return;
    }

    sb->mnt = &mnt;
    mr_super_block_detach_mnt(sb);
    MR_TEST_EXPECT_EQ_PTR(sb->mnt, MR_NULL);

    mr_dentry_del(&mnt);
    mr_super_block_del(sb);
    mr_file_system_del(&fs);
}
MR_TEST_EXPORT(sb, detach_mnt, test_sb_detach_mnt);

static void test_sb_root_inode_has_sb(void) {
    mr_file_system_t fs;
    mr_super_block_t *sb;
    mr_err_t ret;

    ret = __sb_setup_fs(&fs, "sb_ri", testfs_mount, testfs_kill_sb);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    sb = mr_super_block_create(&fs, MR_NULL, 0, MR_NULL);
    MR_TEST_ASSERT_NE_PTR(sb, MR_NULL);
    if (!sb) {
        mr_file_system_del(&fs);
        return;
    }

    /* The root inode should be back-linked to the super-block */
    MR_TEST_EXPECT_EQ_PTR(sb->root->sb, sb);

    mr_super_block_del(sb);
    mr_file_system_del(&fs);
}
MR_TEST_EXPORT(sb, root_inode_has_sb, test_sb_root_inode_has_sb);
#endif /* defined(MR_USE_TEST) && defined(MR_USE_FS) */
