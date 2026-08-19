/**
 * @copyright (c) 2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2026-08-15        MacRsh        First version
 */

#include <mrX/test/mr_test.h>
#if defined(MR_USE_TEST) && defined(MR_USE_FS)
#include <mrX.h>
#include "fs_test.h"

static void fs_at_expect(volatile mr_uint32_t *__test_status, const char *name,
                         mr_err_t actual, mr_err_t expected) {
    MR_IGNORE(name);

    MR_TEST_EXPECT_EQ_INT(actual, expected);
}

static void fs_at_stat_expect(volatile mr_uint32_t *__test_status,
                              const mr_file_t *dir, const char *path, int flags,
                              mr_err_t expected, mr_inode_type_t type) {
    mr_stat_t stat;
    mr_err_t ret;

    /* Get status through the directory-relative API */
    ret = mr_fs_statat_by_path(dir, path, &stat, flags);
    fs_at_expect(__test_status, path, ret, expected);
    if ((ret == 0) && (stat.type != type)) {
        MR_TEST_EXPECT_EQ_INT(stat.type, type);
    }
    return;
}

static void fs_at_path_expect(volatile mr_uint32_t *__test_status,
                              const char *path, mr_err_t expected,
                              mr_inode_type_t type) {
    mr_stat_t stat;
    mr_err_t ret;

    /* Get status through the current namespace */
    ret = mr_fs_stat_by_path(path, &stat);
    fs_at_expect(__test_status, path, ret, expected);
    if ((ret == 0) && (stat.type != type)) {
        MR_TEST_EXPECT_EQ_INT(stat.type, type);
    }
    return;
}

static void fs_at_test(volatile mr_uint32_t *__test_status) {
    mr_file_t mounted_dir = MR_FILE_INIT(mounted_dir);
    mr_file_t regular = MR_FILE_INIT(regular);
    mr_file_t target = MR_FILE_INIT(target);
    mr_file_t base = MR_FILE_INIT(base);
    mr_file_t file = MR_FILE_INIT(file);
    int private_data;
    mr_err_t ret;

    /* Mount RAMFS and create directory handles */
    ret = fs_test_mount_root("ramfs", 0, MR_NULL);
    fs_at_expect(__test_status, "mount ramfs", ret, 0);
    if (ret != 0) {
        return;
    }
    fs_at_expect(__test_status, "mkdir base", mr_fs_mkdir("/base"), 0);
    fs_at_expect(__test_status, "mkdir target", mr_fs_mkdir("/target"), 0);
    ret = mr_fs_opendir(&base, "/base");
    fs_at_expect(__test_status, "open base", ret, 0);
    ret = mr_fs_opendir(&target, "/target");
    fs_at_expect(__test_status, "open target", ret, 0);
    if ((!base.inode) || (!target.inode)) {
        goto _exit;
    }

    /* Create directories relative to a handle */
    fs_at_expect(__test_status, "mkdirat child",
                 mr_fs_mkdirat(&base, "child", 0), 0);
    fs_at_path_expect(__test_status, "/base/child", 0, MR_INODE_DIR);
    fs_at_expect(__test_status, "mkdirat absolute",
                 mr_fs_mkdirat(MR_NULL, "/absolute", 0), 0);
    fs_at_path_expect(__test_status, "/absolute", 0, MR_INODE_DIR);

    /* Confine parent and absolute paths to the directory handle */
    fs_at_expect(__test_status, "mkdirat escape",
                 mr_fs_mkdirat(&base, "../escaped", 0), 0);
    fs_at_path_expect(__test_status, "/escaped", 0, MR_INODE_DIR);
    fs_at_expect(__test_status, "mkdirat parent clamp",
                 mr_fs_mkdirat(&base, "../clamped", MR_AT_IN_ROOT), 0);
    fs_at_path_expect(__test_status, "/base/clamped", 0, MR_INODE_DIR);
    fs_at_expect(__test_status, "mkdirat absolute in-root",
                 mr_fs_mkdirat(&base, "/rooted", MR_AT_IN_ROOT), 0);
    fs_at_path_expect(__test_status, "/base/rooted", 0, MR_INODE_DIR);
    fs_at_path_expect(__test_status, "/rooted", -MR_ENOENT, MR_INODE_NONE);

    /* Create a file-system-specific node relative to the base */
    private_data = 1;
    fs_at_expect(__test_status, "mknodat node",
                 mr_fs_mknodat(&base, "node", MR_INODE_CHR, &private_data, 0),
                 0);
    fs_at_stat_expect(__test_status, &base, "node", 0, 0, MR_INODE_CHR);

    /* Select whether stat follows the final symbolic link */
    fs_at_expect(__test_status, "create symbolic link",
                 mr_fs_symlink("child", "/base/link"), 0);
    fs_at_stat_expect(__test_status, &base, "link", 0, 0, MR_INODE_DIR);
    fs_at_stat_expect(__test_status, &base, "link", MR_AT_SYMLINK_NOFOLLOW, 0,
                      MR_INODE_LNK);
    fs_at_stat_expect(__test_status, &base, "/link",
                      MR_AT_IN_ROOT | MR_AT_SYMLINK_NOFOLLOW, 0, MR_INODE_LNK);

    /* Rename between two directory-relative contexts */
    ret = mr_fs_openat(&file, &base, "source", MR_O_RDWR | MR_O_CREAT);
    fs_at_expect(__test_status, "create source", ret, 0);
    if (ret == 0) {
        fs_at_expect(__test_status, "close source", mr_fs_close(&file), 0);
    }
    fs_at_expect(__test_status, "renameat between directories",
                 mr_fs_renameat_by_path(&base, "source", &target, "moved", 0),
                 0);
    fs_at_path_expect(__test_status, "/base/source", -MR_ENOENT, MR_INODE_NONE);
    fs_at_path_expect(__test_status, "/target/moved", 0, MR_INODE_REG);

    /* Confine rename destinations and unlink symbolic links themselves */
    ret = mr_fs_openat(&file, &base, "confined", MR_O_RDWR | MR_O_CREAT);
    fs_at_expect(__test_status, "create confined", ret, 0);
    if (ret == 0) {
        fs_at_expect(__test_status, "close confined", mr_fs_close(&file), 0);
    }
    fs_at_expect(__test_status, "renameat parent clamp",
                 mr_fs_renameat_by_path(&base, "confined", &base, "../renamed",
                                        MR_AT_IN_ROOT),
                 0);
    fs_at_path_expect(__test_status, "/base/renamed", 0, MR_INODE_REG);
    fs_at_path_expect(__test_status, "/renamed", -MR_ENOENT, MR_INODE_NONE);
    fs_at_expect(__test_status, "unlinkat link",
                 mr_fs_unlinkat_by_path(&base, "link", 0), 0);
    fs_at_path_expect(__test_status, "/base/link", -MR_ENOENT, MR_INODE_NONE);
    fs_at_path_expect(__test_status, "/base/child", 0, MR_INODE_DIR);
    fs_at_expect(__test_status, "unlinkat confined root",
                 mr_fs_unlinkat_by_path(&base, ".", MR_AT_IN_ROOT), -MR_EBUSY);
    fs_at_expect(
        __test_status, "renameat confined root",
        mr_fs_renameat_by_path(&base, ".", &base, "bad", MR_AT_IN_ROOT),
        -MR_EBUSY);
    fs_at_expect(__test_status, "unlinkat moved",
                 mr_fs_unlinkat_by_path(&target, "moved", 0), 0);
    fs_at_path_expect(__test_status, "/target/moved", -MR_ENOENT,
                      MR_INODE_NONE);

    /* Reject invalid bases and unsupported flags */
    ret = mr_fs_open(&regular, "/base/renamed", MR_O_RDONLY);
    fs_at_expect(__test_status, "open regular", ret, 0);
    if (ret == 0) {
        fs_at_expect(__test_status, "mkdirat regular",
                     mr_fs_mkdirat(&regular, "bad", 0), -MR_ENOTDIR);
        fs_at_expect(__test_status, "close regular", mr_fs_close(&regular), 0);
    }
    fs_at_expect(__test_status, "statat null relative",
                 mr_fs_statat_by_path(MR_NULL, "child", &(mr_stat_t){0}, 0),
                 -MR_EBADF);
    fs_at_expect(__test_status, "statat invalid flags",
                 mr_fs_statat_by_path(&base, "child", &(mr_stat_t){0}, 0x10000),
                 -MR_EINVAL);
    fs_at_expect(__test_status, "mkdirat invalid flags",
                 mr_fs_mkdirat(&base, "bad", 0x10000), -MR_EINVAL);
    fs_at_expect(__test_status, "mkdirat null relative",
                 mr_fs_mkdirat(MR_NULL, "bad", 0), -MR_EBADF);
    fs_at_expect(__test_status, "mknodat invalid type",
                 mr_fs_mknodat(&base, "bad", MR_INODE_NONE, MR_NULL, 0),
                 -MR_EINVAL);
    fs_at_expect(__test_status, "mknodat invalid flags",
                 mr_fs_mknodat(&base, "bad", MR_INODE_CHR, MR_NULL, 0x10000),
                 -MR_EINVAL);
    fs_at_expect(__test_status, "mknodat null relative",
                 mr_fs_mknodat(MR_NULL, "bad", MR_INODE_CHR, MR_NULL, 0),
                 -MR_EBADF);
    fs_at_expect(__test_status, "unlinkat invalid flags",
                 mr_fs_unlinkat_by_path(&base, "renamed", 0x10000), -MR_EINVAL);
    fs_at_expect(
        __test_status, "renameat invalid flags",
        mr_fs_renameat_by_path(&base, "renamed", &base, "gone", 0x10000),
        -MR_EINVAL);

    /* Confine mknodat and unlinkat to the directory handle */
    fs_at_expect(__test_status, "mknodat parent clamp",
                 mr_fs_mknodat(&base, "../chr", MR_INODE_CHR, &private_data,
                               MR_AT_IN_ROOT),
                 0);
    fs_at_path_expect(__test_status, "/base/chr", 0, MR_INODE_CHR);
    fs_at_path_expect(__test_status, "/chr", -MR_ENOENT, MR_INODE_NONE);
    fs_at_expect(__test_status, "mknodat absolute in-root",
                 mr_fs_mknodat(&base, "/blk", MR_INODE_BLK, &private_data,
                               MR_AT_IN_ROOT),
                 0);
    fs_at_path_expect(__test_status, "/base/blk", 0, MR_INODE_BLK);
    fs_at_expect(__test_status, "unlinkat parent clamp",
                 mr_fs_unlinkat_by_path(&base, "../chr", MR_AT_IN_ROOT), 0);
    fs_at_path_expect(__test_status, "/base/chr", -MR_ENOENT, MR_INODE_NONE);
    fs_at_expect(__test_status, "unlinkat absolute in-root",
                 mr_fs_unlinkat_by_path(&base, "/blk", MR_AT_IN_ROOT), 0);
    fs_at_path_expect(__test_status, "/base/blk", -MR_ENOENT, MR_INODE_NONE);

    /* Reject a non-empty directory and remove an empty one */
    fs_at_expect(__test_status, "mkdirat empty",
                 mr_fs_mkdirat(&base, "empty", 0), 0);
    fs_at_expect(__test_status, "mkdirat nested",
                 mr_fs_mkdirat(&base, "empty/nested", 0), 0);
    fs_at_expect(__test_status, "unlinkat nonempty",
                 mr_fs_unlinkat_by_path(&base, "empty", 0), -MR_ENOTEMPTY);
    fs_at_expect(__test_status, "unlinkat nested",
                 mr_fs_unlinkat_by_path(&base, "empty/nested", 0), 0);
    fs_at_expect(__test_status, "unlinkat empty",
                 mr_fs_unlinkat_by_path(&base, "empty", 0), 0);
    fs_at_path_expect(__test_status, "/base/empty", -MR_ENOENT, MR_INODE_NONE);

    /* Rename between mixed absolute and directory-relative paths */
    ret = mr_fs_openat(&file, &base, "left", MR_O_RDWR | MR_O_CREAT);
    fs_at_expect(__test_status, "create left", ret, 0);
    if (ret == 0) {
        fs_at_expect(__test_status, "close left", mr_fs_close(&file), 0);
    }
    ret = mr_fs_openat(&file, &base, "right", MR_O_RDWR | MR_O_CREAT);
    fs_at_expect(__test_status, "create right", ret, 0);
    if (ret == 0) {
        fs_at_expect(__test_status, "close right", mr_fs_close(&file), 0);
    }
    fs_at_expect(__test_status, "renameat exists",
                 mr_fs_renameat_by_path(&base, "left", &base, "right", 0),
                 -MR_EEXIST);
    fs_at_expect(
        __test_status, "renameat mixed",
        mr_fs_renameat_by_path(MR_NULL, "/base/left", &base, "moved-left", 0),
        0);
    fs_at_path_expect(__test_status, "/base/left", -MR_ENOENT, MR_INODE_NONE);
    fs_at_path_expect(__test_status, "/base/moved-left", 0, MR_INODE_REG);
    fs_at_stat_expect(__test_status, MR_NULL, "/base/moved-left", 0, 0,
                      MR_INODE_REG);
    fs_at_stat_expect(__test_status, &base, "/moved-left", MR_AT_IN_ROOT, 0,
                      MR_INODE_REG);

    /* Confine *at symlink restarts to the directory handle */
    fs_at_expect(__test_status, "mkdir outside", mr_fs_mkdir("/outside"), 0);
    fs_at_expect(__test_status, "create relative escape link",
                 mr_fs_symlink("../outside", "/base/escape-link"), 0);
    fs_at_expect(__test_status, "create absolute escape link",
                 mr_fs_symlink("/outside", "/base/abs-link"), 0);
    fs_at_expect(__test_status, "mkdirat relative escape",
                 mr_fs_mkdirat(&base, "escape-link/x", MR_AT_IN_ROOT),
                 -MR_ENOENT);
    fs_at_expect(__test_status, "mkdirat absolute escape",
                 mr_fs_mkdirat(&base, "abs-link/x", MR_AT_IN_ROOT), -MR_ENOENT);
    fs_at_path_expect(__test_status, "/outside/x", -MR_ENOENT, MR_INODE_NONE);
    fs_at_stat_expect(__test_status, &base, "abs-link", MR_AT_IN_ROOT,
                      -MR_ENOENT, MR_INODE_NONE);
    fs_at_stat_expect(__test_status, &base, "abs-link",
                      MR_AT_IN_ROOT | MR_AT_SYMLINK_NOFOLLOW, 0, MR_INODE_LNK);
    fs_at_expect(__test_status, "unlinkat confined symlink",
                 mr_fs_unlinkat_by_path(&base, "escape-link", MR_AT_IN_ROOT),
                 0);
    fs_at_path_expect(__test_status, "/outside", 0, MR_INODE_DIR);

    /* Clamp a bare parent component to the confined root */
    fs_at_stat_expect(__test_status, &base, "..", MR_AT_IN_ROOT, 0,
                      MR_INODE_DIR);
    fs_at_expect(__test_status, "unlinkat parent",
                 mr_fs_unlinkat_by_path(&base, "..", MR_AT_IN_ROOT), -MR_EBUSY);

    /* Select whether a dangling symbolic link is followed */
    fs_at_expect(__test_status, "create dangling link",
                 mr_fs_symlink("missing", "/base/dangle"), 0);
    fs_at_stat_expect(__test_status, &base, "dangle", 0, -MR_ENOENT,
                      MR_INODE_NONE);
    fs_at_stat_expect(__test_status, &base, "dangle", MR_AT_SYMLINK_NOFOLLOW, 0,
                      MR_INODE_LNK);
    fs_at_expect(__test_status, "create final dangling link",
                 mr_fs_symlink("missing", "/base/child/final"), 0);
    fs_at_expect(__test_status, "create directory link",
                 mr_fs_symlink("child", "/base/dir-link"), 0);
    fs_at_stat_expect(__test_status, &base, "dir-link/final",
                      MR_AT_SYMLINK_NOFOLLOW, 0, MR_INODE_LNK);

    /* Reject create-side leaf errors through the directory-relative APIs */
    fs_at_expect(__test_status, "mkdirat exists",
                 mr_fs_mkdirat(&base, "child", 0), -MR_EEXIST);
    fs_at_expect(__test_status, "mkdirat missing parent",
                 mr_fs_mkdirat(&base, "nope/leaf", 0), -MR_ENOENT);
    fs_at_expect(__test_status, "mkdirat dot", mr_fs_mkdirat(&base, ".", 0),
                 -MR_EINVAL);
    fs_at_expect(__test_status, "mkdirat dotdot", mr_fs_mkdirat(&base, "..", 0),
                 -MR_EINVAL);
    fs_at_expect(__test_status, "mknodat exists",
                 mr_fs_mknodat(&base, "node", MR_INODE_CHR, MR_NULL, 0),
                 -MR_EEXIST);
    fs_at_expect(__test_status, "mknodat missing parent",
                 mr_fs_mknodat(&base, "nope/node", MR_INODE_CHR, MR_NULL, 0),
                 -MR_ENOENT);
    fs_at_expect(__test_status, "mknodat dot",
                 mr_fs_mknodat(&base, ".", MR_INODE_CHR, MR_NULL, 0),
                 -MR_EINVAL);

    /* Reject cross-file-system rename and stale directory handles */
    fs_at_expect(__test_status, "mkdir mount", mr_fs_mkdir("/mount"), 0);
    fs_at_expect(__test_status, "mount nested",
                 mr_fs_mount(MR_NULL, "/mount", "ramfs", 0, MR_NULL), 0);
    ret = mr_fs_opendir(&mounted_dir, "/mount");
    fs_at_expect(__test_status, "open mounted directory", ret, 0);
    if (ret == 0) {
        fs_at_expect(__test_status, "renameat cross FS",
                     mr_fs_renameat_by_path(&base, "renamed", &mounted_dir,
                                            "renamed", 0),
                     -MR_EXDEV);
        fs_at_expect(__test_status, "umount nested", mr_fs_umount("/mount"), 0);
        fs_at_expect(__test_status, "mkdirat stale directory",
                     mr_fs_mkdirat(&mounted_dir, "stale", 0), -MR_ENOENT);
        fs_at_expect(__test_status, "close mounted directory",
                     mr_fs_closedir(&mounted_dir), 0);
    }

_exit:
    /* Close directory handles and unmount RAMFS */
    if (target.inode) {
        fs_at_expect(__test_status, "close target", mr_fs_closedir(&target), 0);
    }
    if (base.inode) {
        fs_at_expect(__test_status, "close base", mr_fs_closedir(&base), 0);
    }
    fs_at_expect(__test_status, "umount ramfs", mr_fs_umount("/"), 0);
    return;
}

MR_TEST(fs, at) {
    fs_at_test(__test_status);
}

#endif /* defined(MR_USE_TEST) && defined(MR_USE_FS) */
