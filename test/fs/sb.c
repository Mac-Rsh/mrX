/**
 * @copyright (c) 2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2026-08-29        MacRsh        First version
 */

#include <mrX/test/mr_test.h>
#if defined(MR_USE_TEST) && defined(MR_USE_FS)
#include <mrX.h>

static mr_size_t __sb_unmount_calls;

static mr_err_t sb_mount_fail(mr_sblock_t *sb, const char *source,
                              int flags, void *data) {
    MR_IGNORE(sb);
    MR_IGNORE(source);
    MR_IGNORE(flags);
    MR_IGNORE(data);

    return -MR_EIO;
}

static mr_err_t sb_mount_bad_root(mr_sblock_t *sb, const char *source,
                                  int flags, void *data) {
    MR_IGNORE(source);
    MR_IGNORE(flags);
    MR_IGNORE(data);

    sb->root = mr_inode_create(MR_INODE_REG, MR_NULL, MR_NULL, MR_NULL);
    return sb->root ? 0 : -MR_ENOMEM;
}

static mr_err_t sb_mount_good_root(mr_sblock_t *sb, const char *source,
                                   int flags, void *data) {
    MR_IGNORE(source);
    MR_IGNORE(flags);
    MR_IGNORE(data);

    sb->root = mr_inode_create(MR_INODE_DIR, MR_NULL, MR_NULL, MR_NULL);
    return sb->root ? 0 : -MR_ENOMEM;
}

static void sb_unmount(mr_sblock_t *sb) {
    MR_IGNORE(sb);

    __sb_unmount_calls++;
}

static mr_file_system_t __sb_fail_fs
    = MR_FILE_SYSTEM_INIT("sb-fail", sb_mount_fail, sb_unmount);
static mr_file_system_t __sb_bad_root_fs
    = MR_FILE_SYSTEM_INIT("sb-bad-root", sb_mount_bad_root, sb_unmount);
static mr_file_system_t __sb_good_root_fs
    = MR_FILE_SYSTEM_INIT("sb-good-root", sb_mount_good_root, sb_unmount);

static void sb_expect(volatile mr_uint32_t *__test_status, const char *name,
                      mr_int64_t actual, mr_int64_t expected) {
    MR_IGNORE(name);

    MR_TEST_EXPECT_EQ_INT(actual, expected);
}

/* Test: super-block creation rejects invalid roots and cleans up exactly once
 */
MR_TEST(fs, sblock_lifecycle) {
    mr_sblock_t *sb;
    mr_err_t ret;

    /* A failed mount does not produce an object or an unmount callback */
    __sb_unmount_calls = 0;
    sb = MR_NULL;
    ret = mr_sblock_create(&__sb_fail_fs, MR_NULL, 0, MR_NULL, &sb);
    sb_expect(__test_status, "reject failed mount", ret, -MR_EIO);
    sb_expect(__test_status, "failed mount output", sb == MR_NULL, MR_TRUE);
    sb_expect(__test_status, "failed mount unmount count",
              (mr_int64_t)__sb_unmount_calls, 0);

    /* A non-directory root is rejected and released through unmount */
    __sb_unmount_calls = 0;
    sb = MR_NULL;
    ret = mr_sblock_create(&__sb_bad_root_fs, MR_NULL, 0, MR_NULL, &sb);
    sb_expect(__test_status, "reject non-directory root", ret, -MR_ENOTDIR);
    sb_expect(__test_status, "bad root output", sb == MR_NULL, MR_TRUE);
    sb_expect(__test_status, "bad root unmount count",
              (mr_int64_t)__sb_unmount_calls, 1);

    /* An unattached super-block rejects namespace operations before deletion */
    __sb_unmount_calls = 0;
    sb = MR_NULL;
    ret = mr_sblock_create(&__sb_good_root_fs, MR_NULL, 0, MR_NULL, &sb);
    sb_expect(__test_status, "create detached super-block", ret, 0);
    if (!sb) {
        return;
    }
    sb_expect(__test_status, "enter detached super-block",
              mr_sblock_op_enter(sb), -MR_ENOENT);
    sb_expect(__test_status, "delete detached super-block",
              mr_sblock_del(sb), 0);
    sb_expect(__test_status, "detached super-block unmount count",
              (mr_int64_t)__sb_unmount_calls, 1);
}

#endif /* defined(MR_USE_TEST) && defined(MR_USE_FS) */
