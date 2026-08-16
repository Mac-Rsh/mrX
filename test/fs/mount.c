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
#include <mrX/fs/mr_nsp.h>
#include "fs_test.h"

/* Failing filesystem mount state */
static mr_size_t __fs_mount_fail_calls;
static mr_bool_t __fs_unmount_reject;

static mr_err_t fs_mount_fail_mount(mr_super_block_t *sb, const char *source,
                                    int flags, void *data);
static mr_err_t fs_unmount_test_mount(mr_super_block_t *sb, const char *source,
                                      int flags, void *data);
static mr_err_t fs_unmount_test_unmount(mr_super_block_t *sb);

static mr_file_system_t __fs_mount_fail
    = MR_FILE_SYSTEM_INIT("mountfail", fs_mount_fail_mount, MR_NULL, MR_NULL);
static mr_file_system_t __fs_unmount_test = MR_FILE_SYSTEM_INIT(
    "unmounttest", fs_unmount_test_mount, fs_unmount_test_unmount, MR_NULL);

static int fs_mount_expect(const char *name, mr_int64_t actual,
                           mr_int64_t expected) {
    MR_IGNORE(name);

    MR_TEST_EXPECT_EQ_INT(actual, expected);
    return (actual == expected) ? 0 : 1;
}

static int fs_mount_stat_expect(const char *path, mr_err_t expected) {
    mr_stat_t stat;

    return fs_mount_expect(path, fs_test_stat(path, &stat), expected);
}

static int fs_mount_cwd_expect(const char *expected) {
    char cwd[MR_CFG_FS_PATH_LEN];
    mr_ssize_t ret;
    int failures;

    /* Get current directory */
    ret = mr_fs_getcwd(cwd, sizeof(cwd));
    failures = fs_mount_expect("getcwd length", ret,
                               (mr_int64_t)mr_strlen(expected));
    if ((ret >= 0) && (mr_strcmp(cwd, expected) != 0)) {
        MR_TEST_EXPECT_EQ_INT(mr_strcmp(cwd, expected), 0);
        failures++;
    }
    return failures;
}

static mr_err_t fs_mount_fail_mount(mr_super_block_t *sb, const char *source,
                                    int flags, void *data) {
    MR_IGNORE(sb);
    MR_IGNORE(source);
    MR_IGNORE(flags);
    MR_IGNORE(data);

    /* Record the callback before rejecting the mount */
    __fs_mount_fail_calls++;
    return -MR_ENOMEM;
}

static mr_err_t fs_unmount_test_mount(mr_super_block_t *sb, const char *source,
                                      int flags, void *data) {
    MR_IGNORE(source);
    MR_IGNORE(flags);
    MR_IGNORE(data);

    /* Create a minimal root directory */
    sb->root = mr_inode_create(MR_INODE_DIR, MR_NULL, MR_NULL, MR_NULL);
    return sb->root ? 0 : -MR_ENOMEM;
}

static mr_err_t fs_unmount_test_unmount(mr_super_block_t *sb) {
    MR_IGNORE(sb);

    /* Reject unmount while requested */
    return __fs_unmount_reject ? -MR_EIO : 0;
}

static void fs_mount_restore_nsp(mr_dentry_t *root, mr_dentry_t *cwd) {
    mr_fs_nsp_t *nsp;
    mr_dentry_t *old_root, *old_cwd;
    int mask;

    /* Get current file-system namespace */
    nsp = mr_fs_nsp_current();
    if (!nsp) {
        return;
    }

    /* Restore saved context */
    mask = mr_fs_nsp_lock();
    old_root = nsp->root;
    old_cwd = nsp->cwd;
    nsp->root = root;
    nsp->cwd = cwd;
    mr_fs_nsp_unlock(mask);

    /* Drop replaced namespace references */
    mr_dentry_put(old_root);
    mr_dentry_put(old_cwd);
}

static int fs_mount_test(void) {
    mr_fs_nsp_t *nsp;
    mr_dentry_t *saved_root, *saved_cwd;
    mr_file_t file = MR_FILE_INIT(file);
    mr_file_t directory = MR_FILE_INIT(directory);
    int failures = 0;
    int mask;
    mr_err_t ret;

    /* Register a filesystem which rejects mount requests */
    failures += fs_mount_expect("register mountfail",
                                mr_file_system_register(&__fs_mount_fail), 0);
    failures += fs_mount_expect("register unmounttest",
                                mr_file_system_register(&__fs_unmount_test), 0);

    /* Mount the root filesystem */
    ret = mr_fs_mount(MR_NULL, "/", "ramfs", 0, MR_NULL);
    failures += fs_mount_expect("mount ramfs", ret, 0);
    if (ret != 0) {
        return failures;
    }

    /* Check mount request validation */
    failures += fs_mount_expect("repeat root mount",
                                mr_fs_mount(MR_NULL, "/", "ramfs", 0, MR_NULL),
                                -MR_EBUSY);
    failures += fs_mount_expect(
        "unknown filesystem",
        mr_fs_mount(MR_NULL, "/missing", "unknown", 0, MR_NULL), -MR_ENOENT);
    failures += fs_mount_expect("mkdir plain", mr_fs_mkdir("/plain"), 0);
    failures
        += fs_mount_expect("umount plain", mr_fs_umount("/plain"), -MR_ENOENT);
    ret = mr_fs_open(&file, "/regular", MR_O_RDWR | MR_O_CREAT);
    failures += fs_mount_expect("create regular", ret, 0);
    if (ret == 0) {
        failures += fs_mount_expect("close regular", mr_fs_close(&file), 0);
    }
    failures += fs_mount_expect(
        "mount regular", mr_fs_mount(MR_NULL, "/regular", "ramfs", 0, MR_NULL),
        -MR_ENOTDIR);

    /* Confirm a failed callback does not cover the mount-point */
    failures += fs_mount_expect("mkdir fail", mr_fs_mkdir("/fail"), 0);
    __fs_mount_fail_calls = 0;
    failures += fs_mount_expect(
        "fail mount", mr_fs_mount(MR_NULL, "/fail", "mountfail", 0, MR_NULL),
        -MR_ENOMEM);
    failures += fs_mount_expect("fail mount callback",
                                (mr_int64_t)__fs_mount_fail_calls, 1);
    failures += fs_mount_stat_expect("/fail", 0);
    ret = mr_fs_open(&file, "/fail/uncovered", MR_O_RDWR | MR_O_CREAT);
    failures += fs_mount_expect("create uncovered", ret, 0);
    if (ret == 0) {
        failures += fs_mount_expect("close uncovered", mr_fs_close(&file), 0);
    }
    failures += fs_mount_stat_expect("/fail/uncovered", 0);

    /* Preserve the mount when its backing FS rejects unmount */
    failures += fs_mount_expect("mkdir unmount reject",
                                mr_fs_mkdir("/unmount-reject"), 0);
    failures += fs_mount_expect(
        "mount unmount reject",
        mr_fs_mount(MR_NULL, "/unmount-reject", "unmounttest", 0, MR_NULL), 0);
    __fs_unmount_reject = MR_TRUE;
    failures += fs_mount_expect("propagate unmount rejection",
                                mr_fs_umount("/unmount-reject"), -MR_EIO);
    failures += fs_mount_stat_expect("/unmount-reject", 0);
    ret = mr_fs_opendir(&directory, "/unmount-reject");
    failures += fs_mount_expect("open rejected mount root", ret, 0);
    if (ret == 0) {
        ret = mr_fs_openat(&file, &directory, ".",
                           MR_O_RDONLY | MR_O_DIRECTORY);
        failures += fs_mount_expect("use rejected mount root", ret, 0);
        if (ret == 0) {
            failures += fs_mount_expect("close rejected mount child",
                                        mr_fs_close(&file), 0);
        }
        failures += fs_mount_expect("close rejected mount root",
                                    mr_fs_closedir(&directory), 0);
    }
    __fs_unmount_reject = MR_FALSE;
    failures += fs_mount_expect("retry unmount rejection",
                                mr_fs_umount("/unmount-reject"), 0);

    /* Mount across a covered directory and walk mount boundaries */
    failures += fs_mount_expect("mkdir mount", mr_fs_mkdir("/mount"), 0);
    ret = mr_fs_open(&file, "/mount/covered", MR_O_RDWR | MR_O_CREAT);
    failures += fs_mount_expect("create covered", ret, 0);
    if (ret == 0) {
        failures += fs_mount_expect("close covered", mr_fs_close(&file), 0);
    }
    failures += fs_mount_expect(
        "mount nested root",
        mr_fs_mount(MR_NULL, "/mount", "ramfs", 0, MR_NULL), 0);
    failures += fs_mount_expect(
        "repeat nested mount",
        mr_fs_mount(MR_NULL, "/mount", "ramfs", 0, MR_NULL), -MR_EBUSY);
    failures += fs_mount_stat_expect("/mount/covered", -MR_ENOENT);
    failures
        += fs_mount_expect("mkdir nested", mr_fs_mkdir("/mount/nested"), 0);
    failures += fs_mount_expect(
        "mount child",
        mr_fs_mount(MR_NULL, "/mount/nested", "ramfs", 0, MR_NULL), 0);
    failures += fs_mount_expect("umount busy parent", mr_fs_umount("/mount"),
                                -MR_EBUSY);
    failures += fs_mount_expect("chdir child", mr_fs_chdir("/mount/nested"), 0);
    failures += fs_mount_cwd_expect("/mount/nested");
    failures += fs_mount_expect("chdir child parent", mr_fs_chdir(".."), 0);
    failures += fs_mount_cwd_expect("/mount");
    failures += fs_mount_expect("chdir mount parent", mr_fs_chdir("./.."), 0);
    failures += fs_mount_cwd_expect("/");
    failures += fs_mount_expect("umount child dot",
                                mr_fs_umount("/mount/nested/."), 0);
    failures
        += fs_mount_expect("umount parent dot", mr_fs_umount("/mount/."), 0);
    failures += fs_mount_stat_expect("/mount/covered", 0);

    /* Build a chroot hierarchy and retain the original context for cleanup */
    failures += fs_mount_expect("mkdir jail", mr_fs_mkdir("/jail"), 0);
    failures
        += fs_mount_expect("mkdir jail work", mr_fs_mkdir("/jail/work"), 0);
    failures += fs_mount_expect("mkdir outside", mr_fs_mkdir("/outside"), 0);
    ret = mr_fs_open(&file, "/jail/file", MR_O_RDWR | MR_O_CREAT);
    failures += fs_mount_expect("create jail file", ret, 0);
    if (ret == 0) {
        failures += fs_mount_expect("close jail file", mr_fs_close(&file), 0);
    }
    failures += fs_mount_expect("chdir regular", mr_fs_chdir("/jail/file"),
                                -MR_ENOTDIR);
    failures += fs_mount_expect("chroot regular", mr_fs_chroot("/jail/file"),
                                -MR_ENOTDIR);
    failures += fs_mount_expect(
        "create jail escape link",
        mr_fs_symlink("/outside", "/jail/escape"), 0);
    failures += fs_mount_expect(
        "create jail work link",
        mr_fs_symlink("work", "/jail/work-link"), 0);
    failures += fs_mount_expect("chdir work link",
                                mr_fs_chdir("/jail/work-link"), 0);
    failures += fs_mount_cwd_expect("/jail/work");
    failures += fs_mount_expect("chdir outside", mr_fs_chdir("/outside"), 0);

    /* Pin the original context before changing the namespace root */
    nsp = mr_fs_nsp_current();
    saved_root = MR_NULL;
    saved_cwd = MR_NULL;
    if (!nsp) {
        failures++;
    } else {
        mask = mr_fs_nsp_lock();
        saved_root = mr_dentry_get(nsp->root);
        saved_cwd = mr_dentry_get(nsp->cwd);
        mr_fs_nsp_unlock(mask);
        if ((!saved_root) || (!saved_cwd)) {
            failures++;
        }
    }

    /* Constrain absolute paths and parent traversal below the chroot */
    if (saved_root && saved_cwd) {
        failures += fs_mount_expect("chdir jail work",
                                    mr_fs_chdir("/jail/work"), 0);
        failures += fs_mount_expect("chroot retain cwd",
                                    mr_fs_chroot("/jail"), 0);
        failures += fs_mount_cwd_expect("/work");
        failures += fs_mount_stat_expect("/escape", -MR_ENOENT);
        failures += fs_mount_expect("chdir work parent", mr_fs_chdir(".."),
                                    0);
        failures += fs_mount_cwd_expect("/");
        fs_mount_restore_nsp(mr_dentry_get(saved_root),
                             mr_dentry_get(saved_cwd));
        failures += fs_mount_expect("chdir outside again",
                                    mr_fs_chdir("/outside"), 0);
        failures += fs_mount_expect("chroot jail", mr_fs_chroot("/jail"), 0);
        failures += fs_mount_cwd_expect("/");
        failures += fs_mount_stat_expect("/work", 0);
        failures += fs_mount_stat_expect("/outside", -MR_ENOENT);
        failures += fs_mount_stat_expect("/../outside", -MR_ENOENT);
        failures += fs_mount_expect("chdir work", mr_fs_chdir("/work"), 0);
        failures += fs_mount_cwd_expect("/work");
        failures += fs_mount_expect("chdir work dot", mr_fs_chdir("."), 0);
        failures += fs_mount_cwd_expect("/work");
        failures += fs_mount_expect("chdir root parent", mr_fs_chdir("/.."), 0);
        failures += fs_mount_cwd_expect("/");
        failures += fs_mount_expect("chroot work", mr_fs_chroot("/work"), 0);
        failures += fs_mount_cwd_expect("/");
        failures
            += fs_mount_expect("chdir nested parent", mr_fs_chdir(".."), 0);
        failures += fs_mount_cwd_expect("/");

        /* Restore the context before releasing the root filesystem */
        fs_mount_restore_nsp(saved_root, saved_cwd);
        saved_root = MR_NULL;
        saved_cwd = MR_NULL;
    } else {
        if (saved_root) {
            mr_dentry_put(saved_root);
        }
        if (saved_cwd) {
            mr_dentry_put(saved_cwd);
        }
    }

    /* Restore root namespace state */
    failures += fs_mount_expect("chdir root", mr_fs_chdir("/"), 0);

    /* Unmount while the current directory is inside the mount */
    failures += fs_mount_expect("mkdir cwd mount",
                                mr_fs_mkdir("/cwd-mnt"), 0);
    failures += fs_mount_expect(
        "mount cwd", mr_fs_mount(MR_NULL, "/cwd-mnt", "ramfs", 0, MR_NULL),
        0);
    failures += fs_mount_expect("chdir cwd mount",
                                mr_fs_chdir("/cwd-mnt"), 0);
    failures += fs_mount_expect("umount cwd mount",
                                mr_fs_umount("/cwd-mnt"), 0);
    {
        char detached[8];

        failures += fs_mount_expect(
            "getcwd detached",
            (mr_err_t)mr_fs_getcwd(detached, sizeof(detached)),
            -MR_ENOENT);
    }
    failures += fs_mount_expect("chdir root after detach",
                                mr_fs_chdir("/"), 0);
    failures += fs_mount_expect("umount ramfs", mr_fs_umount("/"), 0);
    return failures;
}

static void mount(void) {
    MR_TEST_EXPECT_EQ_INT(fs_mount_test(), 0);
}
MR_TEST_EXPORT(fs, mount, mount);
#endif /* defined(MR_USE_TEST) && defined(MR_USE_FS) */
