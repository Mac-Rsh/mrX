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

static int fs_at_expect(const char *name, mr_err_t actual,
                        mr_err_t expected) {
    MR_IGNORE(name);

    MR_TEST_EXPECT_EQ_INT(actual, expected);
    return (actual == expected) ? 0 : 1;
}

static int fs_at_stat_expect(const mr_file_t *dir, const char *path, int flags,
                             mr_err_t expected, mr_inode_type_t type) {
    mr_stat_t stat;
    mr_err_t ret;
    int failures;

    /* Get status through the directory-relative API */
    ret = mr_fs_statat(dir, path, &stat, flags);
    failures = fs_at_expect(path, ret, expected);
    if ((ret == 0) && (stat.type != type)) {
        MR_TEST_EXPECT_EQ_INT(stat.type, type);
        failures++;
    }
    return failures;
}

static int fs_at_path_expect(const char *path, mr_err_t expected,
                             mr_inode_type_t type) {
    mr_stat_t stat;
    mr_err_t ret;
    int failures;

    /* Get status through the current namespace */
    ret = mr_fs_stat_by_path(path, &stat);
    failures = fs_at_expect(path, ret, expected);
    if ((ret == 0) && (stat.type != type)) {
        MR_TEST_EXPECT_EQ_INT(stat.type, type);
        failures++;
    }
    return failures;
}

static int fs_at_test(void) {
    mr_file_t mounted_dir = MR_FILE_INIT(mounted_dir);
    mr_file_t regular = MR_FILE_INIT(regular);
    mr_file_t target = MR_FILE_INIT(target);
    mr_file_t base = MR_FILE_INIT(base);
    mr_file_t file = MR_FILE_INIT(file);
    int private_data;
    int failures = 0;
    mr_err_t ret;

    /* Mount RAMFS and create directory handles */
    ret = mr_fs_mount(MR_NULL, "/", "ramfs", 0, MR_NULL);
    failures += fs_at_expect("mount ramfs", ret, 0);
    if (ret != 0) {
        return failures;
    }
    failures += fs_at_expect("mkdir base", mr_fs_mkdir("/base"), 0);
    failures += fs_at_expect("mkdir target", mr_fs_mkdir("/target"), 0);
    ret = mr_fs_opendir(&base, "/base");
    failures += fs_at_expect("open base", ret, 0);
    ret = mr_fs_opendir(&target, "/target");
    failures += fs_at_expect("open target", ret, 0);
    if ((!base.inode) || (!target.inode)) {
        goto _exit;
    }

    /* Create directories relative to a handle */
    failures += fs_at_expect("mkdirat child",
                             mr_fs_mkdirat(&base, "child", 0), 0);
    failures += fs_at_path_expect("/base/child", 0, MR_INODE_DIR);
    failures += fs_at_expect("mkdirat absolute",
                             mr_fs_mkdirat(MR_NULL, "/absolute", 0), 0);
    failures += fs_at_path_expect("/absolute", 0, MR_INODE_DIR);

    /* Confine parent and absolute paths to the directory handle */
    failures += fs_at_expect("mkdirat escape",
                             mr_fs_mkdirat(&base, "../escaped", 0), 0);
    failures += fs_at_path_expect("/escaped", 0, MR_INODE_DIR);
    failures += fs_at_expect(
        "mkdirat parent clamp",
        mr_fs_mkdirat(&base, "../clamped", MR_AT_IN_ROOT), 0);
    failures += fs_at_path_expect("/base/clamped", 0, MR_INODE_DIR);
    failures += fs_at_expect(
        "mkdirat absolute in-root",
        mr_fs_mkdirat(&base, "/rooted", MR_AT_IN_ROOT), 0);
    failures += fs_at_path_expect("/base/rooted", 0, MR_INODE_DIR);
    failures += fs_at_path_expect("/rooted", -MR_ENOENT, MR_INODE_NONE);

    /* Create a file-system-specific node relative to the base */
    private_data = 1;
    failures += fs_at_expect(
        "mknodat node",
        mr_fs_mknodat(&base, "node", MR_INODE_CHR, &private_data, 0), 0);
    failures += fs_at_stat_expect(&base, "node", 0, 0, MR_INODE_CHR);

    /* Select whether stat follows the final symbolic link */
    failures += fs_at_expect("create symbolic link",
                             mr_fs_symlink("child", "/base/link"), 0);
    failures += fs_at_stat_expect(&base, "link", 0, 0, MR_INODE_DIR);
    failures += fs_at_stat_expect(&base, "link",
                                  MR_AT_SYMLINK_NOFOLLOW, 0, MR_INODE_LNK);
    failures += fs_at_stat_expect(&base, "/link",
                                  MR_AT_IN_ROOT | MR_AT_SYMLINK_NOFOLLOW, 0,
                                  MR_INODE_LNK);

    /* Rename between two directory-relative contexts */
    ret = mr_fs_openat(&file, &base, "source", MR_O_RDWR | MR_O_CREAT);
    failures += fs_at_expect("create source", ret, 0);
    if (ret == 0) {
        failures += fs_at_expect("close source", mr_fs_close(&file), 0);
    }
    failures += fs_at_expect(
        "renameat between directories",
        mr_fs_renameat(&base, "source", &target, "moved", 0), 0);
    failures += fs_at_path_expect("/base/source", -MR_ENOENT,
                                  MR_INODE_NONE);
    failures += fs_at_path_expect("/target/moved", 0, MR_INODE_REG);

    /* Confine rename destinations and unlink symbolic links themselves */
    ret = mr_fs_openat(&file, &base, "confined", MR_O_RDWR | MR_O_CREAT);
    failures += fs_at_expect("create confined", ret, 0);
    if (ret == 0) {
        failures += fs_at_expect("close confined", mr_fs_close(&file), 0);
    }
    failures += fs_at_expect(
        "renameat parent clamp",
        mr_fs_renameat(&base, "confined", &base, "../renamed",
                       MR_AT_IN_ROOT),
        0);
    failures += fs_at_path_expect("/base/renamed", 0, MR_INODE_REG);
    failures += fs_at_path_expect("/renamed", -MR_ENOENT, MR_INODE_NONE);
    failures += fs_at_expect("unlinkat link",
                             mr_fs_unlinkat(&base, "link", 0), 0);
    failures += fs_at_path_expect("/base/link", -MR_ENOENT, MR_INODE_NONE);
    failures += fs_at_path_expect("/base/child", 0, MR_INODE_DIR);
    failures += fs_at_expect(
        "unlinkat confined root",
        mr_fs_unlinkat(&base, ".", MR_AT_IN_ROOT), -MR_EBUSY);
    failures += fs_at_expect(
        "renameat confined root",
        mr_fs_renameat(&base, ".", &base, "bad", MR_AT_IN_ROOT),
        -MR_EBUSY);
    failures += fs_at_expect("unlinkat moved",
                             mr_fs_unlinkat(&target, "moved", 0), 0);
    failures += fs_at_path_expect("/target/moved", -MR_ENOENT,
                                  MR_INODE_NONE);

    /* Reject invalid bases and unsupported flags */
    ret = mr_fs_open(&regular, "/base/renamed", MR_O_RDONLY);
    failures += fs_at_expect("open regular", ret, 0);
    if (ret == 0) {
        failures += fs_at_expect("mkdirat regular",
                                 mr_fs_mkdirat(&regular, "bad", 0),
                                 -MR_ENOTDIR);
        failures += fs_at_expect("close regular", mr_fs_close(&regular), 0);
    }
    failures += fs_at_expect("statat null relative",
                             mr_fs_statat(MR_NULL, "child", &(mr_stat_t){0}, 0),
                             -MR_EBADF);
    failures += fs_at_expect(
        "statat invalid flags",
        mr_fs_statat(&base, "child", &(mr_stat_t){0}, 0x10000), -MR_EINVAL);
    failures += fs_at_expect("mkdirat invalid flags",
                             mr_fs_mkdirat(&base, "bad", 0x10000),
                             -MR_EINVAL);
    failures += fs_at_expect("mkdirat null relative",
                             mr_fs_mkdirat(MR_NULL, "bad", 0), -MR_EBADF);
    failures += fs_at_expect(
        "mknodat invalid type",
        mr_fs_mknodat(&base, "bad", MR_INODE_NONE, MR_NULL, 0), -MR_EINVAL);
    failures += fs_at_expect(
        "mknodat invalid flags",
        mr_fs_mknodat(&base, "bad", MR_INODE_CHR, MR_NULL, 0x10000),
        -MR_EINVAL);
    failures += fs_at_expect(
        "mknodat null relative",
        mr_fs_mknodat(MR_NULL, "bad", MR_INODE_CHR, MR_NULL, 0), -MR_EBADF);
    failures += fs_at_expect("unlinkat invalid flags",
                             mr_fs_unlinkat(&base, "renamed", 0x10000),
                             -MR_EINVAL);
    failures += fs_at_expect(
        "renameat invalid flags",
        mr_fs_renameat(&base, "renamed", &base, "gone", 0x10000),
        -MR_EINVAL);

    /* Confine mknodat and unlinkat to the directory handle */
    failures += fs_at_expect(
        "mknodat parent clamp",
        mr_fs_mknodat(&base, "../chr", MR_INODE_CHR, &private_data,
                      MR_AT_IN_ROOT),
        0);
    failures += fs_at_path_expect("/base/chr", 0, MR_INODE_CHR);
    failures += fs_at_path_expect("/chr", -MR_ENOENT, MR_INODE_NONE);
    failures += fs_at_expect(
        "mknodat absolute in-root",
        mr_fs_mknodat(&base, "/blk", MR_INODE_BLK, &private_data,
                      MR_AT_IN_ROOT),
        0);
    failures += fs_at_path_expect("/base/blk", 0, MR_INODE_BLK);
    failures += fs_at_expect("unlinkat parent clamp",
                             mr_fs_unlinkat(&base, "../chr", MR_AT_IN_ROOT),
                             0);
    failures += fs_at_path_expect("/base/chr", -MR_ENOENT, MR_INODE_NONE);
    failures += fs_at_expect("unlinkat absolute in-root",
                             mr_fs_unlinkat(&base, "/blk", MR_AT_IN_ROOT),
                             0);
    failures += fs_at_path_expect("/base/blk", -MR_ENOENT, MR_INODE_NONE);

    /* Reject a non-empty directory and remove an empty one */
    failures += fs_at_expect("mkdirat empty",
                             mr_fs_mkdirat(&base, "empty", 0), 0);
    failures += fs_at_expect("mkdirat nested",
                             mr_fs_mkdirat(&base, "empty/nested", 0), 0);
    failures += fs_at_expect("unlinkat nonempty",
                             mr_fs_unlinkat(&base, "empty", 0),
                             -MR_ENOTEMPTY);
    failures += fs_at_expect("unlinkat nested",
                             mr_fs_unlinkat(&base, "empty/nested", 0), 0);
    failures += fs_at_expect("unlinkat empty",
                             mr_fs_unlinkat(&base, "empty", 0), 0);
    failures += fs_at_path_expect("/base/empty", -MR_ENOENT, MR_INODE_NONE);

    /* Rename between mixed absolute and directory-relative paths */
    ret = mr_fs_openat(&file, &base, "left", MR_O_RDWR | MR_O_CREAT);
    failures += fs_at_expect("create left", ret, 0);
    if (ret == 0) {
        failures += fs_at_expect("close left", mr_fs_close(&file), 0);
    }
    ret = mr_fs_openat(&file, &base, "right", MR_O_RDWR | MR_O_CREAT);
    failures += fs_at_expect("create right", ret, 0);
    if (ret == 0) {
        failures += fs_at_expect("close right", mr_fs_close(&file), 0);
    }
    failures += fs_at_expect(
        "renameat exists",
        mr_fs_renameat(&base, "left", &base, "right", 0), -MR_EEXIST);
    failures += fs_at_expect(
        "renameat mixed",
        mr_fs_renameat(MR_NULL, "/base/left", &base, "moved-left", 0), 0);
    failures += fs_at_path_expect("/base/left", -MR_ENOENT, MR_INODE_NONE);
    failures += fs_at_path_expect("/base/moved-left", 0, MR_INODE_REG);
    failures += fs_at_stat_expect(MR_NULL, "/base/moved-left", 0, 0,
                                  MR_INODE_REG);
    failures += fs_at_stat_expect(&base, "/moved-left", MR_AT_IN_ROOT, 0,
                                  MR_INODE_REG);

    /* Confine *at symlink restarts to the directory handle */
    failures += fs_at_expect("mkdir outside", mr_fs_mkdir("/outside"), 0);
    failures += fs_at_expect(
        "create relative escape link",
        mr_fs_symlink("../outside", "/base/escape-link"), 0);
    failures += fs_at_expect(
        "create absolute escape link",
        mr_fs_symlink("/outside", "/base/abs-link"), 0);
    failures += fs_at_expect(
        "mkdirat relative escape",
        mr_fs_mkdirat(&base, "escape-link/x", MR_AT_IN_ROOT), -MR_ENOENT);
    failures += fs_at_expect(
        "mkdirat absolute escape",
        mr_fs_mkdirat(&base, "abs-link/x", MR_AT_IN_ROOT), -MR_ENOENT);
    failures += fs_at_path_expect("/outside/x", -MR_ENOENT, MR_INODE_NONE);
    failures += fs_at_stat_expect(&base, "abs-link", MR_AT_IN_ROOT,
                                  -MR_ENOENT, MR_INODE_NONE);
    failures += fs_at_stat_expect(&base, "abs-link",
                                  MR_AT_IN_ROOT | MR_AT_SYMLINK_NOFOLLOW, 0,
                                  MR_INODE_LNK);
    failures += fs_at_expect(
        "unlinkat confined symlink",
        mr_fs_unlinkat(&base, "escape-link", MR_AT_IN_ROOT), 0);
    failures += fs_at_path_expect("/outside", 0, MR_INODE_DIR);

    /* Clamp a bare parent component to the confined root */
    failures += fs_at_stat_expect(&base, "..", MR_AT_IN_ROOT, 0,
                                  MR_INODE_DIR);
    failures += fs_at_expect("unlinkat parent",
                             mr_fs_unlinkat(&base, "..", MR_AT_IN_ROOT),
                             -MR_EBUSY);

    /* Select whether a dangling symbolic link is followed */
    failures += fs_at_expect("create dangling link",
                             mr_fs_symlink("missing", "/base/dangle"), 0);
    failures += fs_at_stat_expect(&base, "dangle", 0, -MR_ENOENT,
                                  MR_INODE_NONE);
    failures += fs_at_stat_expect(&base, "dangle", MR_AT_SYMLINK_NOFOLLOW, 0,
                                  MR_INODE_LNK);
    failures += fs_at_expect("create final dangling link",
                             mr_fs_symlink("missing", "/base/child/final"),
                             0);
    failures += fs_at_expect("create directory link",
                             mr_fs_symlink("child", "/base/dir-link"), 0);
    failures += fs_at_stat_expect(&base, "dir-link/final",
                                  MR_AT_SYMLINK_NOFOLLOW, 0, MR_INODE_LNK);

    /* Reject create-side leaf errors through the directory-relative APIs */
    failures += fs_at_expect("mkdirat exists",
                             mr_fs_mkdirat(&base, "child", 0), -MR_EEXIST);
    failures += fs_at_expect("mkdirat missing parent",
                             mr_fs_mkdirat(&base, "nope/leaf", 0),
                             -MR_ENOENT);
    failures += fs_at_expect("mkdirat dot",
                             mr_fs_mkdirat(&base, ".", 0), -MR_EINVAL);
    failures += fs_at_expect("mkdirat dotdot",
                             mr_fs_mkdirat(&base, "..", 0), -MR_EINVAL);
    failures += fs_at_expect(
        "mknodat exists",
        mr_fs_mknodat(&base, "node", MR_INODE_CHR, MR_NULL, 0), -MR_EEXIST);
    failures += fs_at_expect(
        "mknodat missing parent",
        mr_fs_mknodat(&base, "nope/node", MR_INODE_CHR, MR_NULL, 0),
        -MR_ENOENT);
    failures += fs_at_expect(
        "mknodat dot",
        mr_fs_mknodat(&base, ".", MR_INODE_CHR, MR_NULL, 0), -MR_EINVAL);

    /* Reject cross-file-system rename and stale directory handles */
    failures += fs_at_expect("mkdir mount", mr_fs_mkdir("/mount"), 0);
    failures += fs_at_expect(
        "mount nested", mr_fs_mount(MR_NULL, "/mount", "ramfs", 0, MR_NULL),
        0);
    ret = mr_fs_opendir(&mounted_dir, "/mount");
    failures += fs_at_expect("open mounted directory", ret, 0);
    if (ret == 0) {
        failures += fs_at_expect(
            "renameat cross FS",
            mr_fs_renameat(&base, "renamed", &mounted_dir, "renamed", 0),
            -MR_EXDEV);
        failures += fs_at_expect("umount nested", mr_fs_umount("/mount"), 0);
        failures += fs_at_expect(
            "mkdirat stale directory",
            mr_fs_mkdirat(&mounted_dir, "stale", 0), -MR_ENOENT);
        failures += fs_at_expect("close mounted directory",
                                 mr_fs_closedir(&mounted_dir), 0);
    }

_exit:
    /* Close directory handles and unmount RAMFS */
    if (target.inode) {
        failures += fs_at_expect("close target", mr_fs_closedir(&target), 0);
    }
    if (base.inode) {
        failures += fs_at_expect("close base", mr_fs_closedir(&base), 0);
    }
    failures += fs_at_expect("umount ramfs", mr_fs_umount("/"), 0);
    return failures;
}

static void at(void) {
    MR_TEST_EXPECT_EQ_INT(fs_at_test(), 0);
}
MR_TEST_EXPORT(fs, at, at);
#endif /* defined(MR_USE_TEST) && defined(MR_USE_FS) */
