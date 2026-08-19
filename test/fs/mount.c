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

static void fs_mount_expect(volatile mr_uint32_t *__test_status,
                            const char *name, mr_int64_t actual,
                            mr_int64_t expected) {
    MR_IGNORE(name);

    MR_TEST_EXPECT_EQ_INT(actual, expected);
}

static void fs_mount_stat_expect(volatile mr_uint32_t *__test_status,
                                 const char *path, mr_err_t expected) {
    mr_stat_t stat;

    fs_mount_expect(__test_status, path, fs_test_stat(path, &stat), expected);
}

static void fs_mount_cwd_expect(volatile mr_uint32_t *__test_status,
                                const char *expected) {
    char cwd[MR_CFG_FS_PATH_LEN];
    mr_ssize_t ret;

    /* Get current directory */
    ret = mr_fs_getcwd(cwd, sizeof(cwd));
    fs_mount_expect(__test_status, "getcwd length", ret,
                    (mr_int64_t)mr_strlen(expected));
    if ((ret >= 0) && (mr_strcmp(cwd, expected) != 0)) {
        MR_TEST_EXPECT_EQ_INT(mr_strcmp(cwd, expected), 0);
    }
    return;
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

static void fs_mount_test(volatile mr_uint32_t *__test_status) {
    mr_fs_nsp_t *nsp;
    mr_dentry_t *saved_root, *saved_cwd;
    mr_file_t file = MR_FILE_INIT(file);
    mr_file_t directory = MR_FILE_INIT(directory);
    int mask;
    mr_err_t ret;

    /* Register a filesystem which rejects mount requests */
    fs_mount_expect(__test_status, "register mountfail",
                    mr_file_system_register(&__fs_mount_fail), 0);
    fs_mount_expect(__test_status, "register unmounttest",
                    mr_file_system_register(&__fs_unmount_test), 0);

    /* Mount the root filesystem */
    ret = fs_test_mount_root("ramfs", 0, MR_NULL);
    fs_mount_expect(__test_status, "mount ramfs", ret, 0);
    if (ret != 0) {
        return;
    }

    /* Check mount request validation */
    fs_mount_expect(__test_status, "repeat root mount",
                    mr_fs_mount(MR_NULL, "/", "ramfs", 0, MR_NULL), -MR_EBUSY);
    fs_mount_expect(__test_status, "unknown filesystem",
                    mr_fs_mount(MR_NULL, "/missing", "unknown", 0, MR_NULL),
                    -MR_ENOENT);
    fs_mount_expect(__test_status, "mkdir plain", mr_fs_mkdir("/plain"), 0);
    fs_mount_expect(__test_status, "umount plain", mr_fs_umount("/plain"),
                    -MR_ENOENT);
    ret = mr_fs_open(&file, "/regular", MR_O_RDWR | MR_O_CREAT);
    fs_mount_expect(__test_status, "create regular", ret, 0);
    if (ret == 0) {
        fs_mount_expect(__test_status, "close regular", mr_fs_close(&file), 0);
    }
    fs_mount_expect(__test_status, "mount regular",
                    mr_fs_mount(MR_NULL, "/regular", "ramfs", 0, MR_NULL),
                    -MR_ENOTDIR);

    /* Confirm a failed callback does not cover the mount-point */
    fs_mount_expect(__test_status, "mkdir fail", mr_fs_mkdir("/fail"), 0);
    __fs_mount_fail_calls = 0;
    fs_mount_expect(__test_status, "fail mount",
                    mr_fs_mount(MR_NULL, "/fail", "mountfail", 0, MR_NULL),
                    -MR_ENOMEM);
    fs_mount_expect(__test_status, "fail mount callback",
                    (mr_int64_t)__fs_mount_fail_calls, 1);
    fs_mount_stat_expect(__test_status, "/fail", 0);
    ret = mr_fs_open(&file, "/fail/uncovered", MR_O_RDWR | MR_O_CREAT);
    fs_mount_expect(__test_status, "create uncovered", ret, 0);
    if (ret == 0) {
        fs_mount_expect(__test_status, "close uncovered", mr_fs_close(&file),
                        0);
    }
    fs_mount_stat_expect(__test_status, "/fail/uncovered", 0);

    /* Preserve the mount when its backing FS rejects unmount */
    fs_mount_expect(__test_status, "mkdir unmount reject",
                    mr_fs_mkdir("/unmount-reject"), 0);
    fs_mount_expect(
        __test_status, "mount unmount reject",
        mr_fs_mount(MR_NULL, "/unmount-reject", "unmounttest", 0, MR_NULL), 0);
    __fs_unmount_reject = MR_TRUE;
    fs_mount_expect(__test_status, "propagate unmount rejection",
                    mr_fs_umount("/unmount-reject"), -MR_EIO);
    fs_mount_stat_expect(__test_status, "/unmount-reject", 0);
    ret = mr_fs_opendir(&directory, "/unmount-reject");
    fs_mount_expect(__test_status, "open rejected mount root", ret, 0);
    if (ret == 0) {
        ret = mr_fs_openat(&file, &directory, ".",
                           MR_O_RDONLY | MR_O_DIRECTORY);
        fs_mount_expect(__test_status, "use rejected mount root", ret, 0);
        if (ret == 0) {
            fs_mount_expect(__test_status, "close rejected mount child",
                            mr_fs_close(&file), 0);
        }
        fs_mount_expect(__test_status, "close rejected mount root",
                        mr_fs_closedir(&directory), 0);
    }
    __fs_unmount_reject = MR_FALSE;
    fs_mount_expect(__test_status, "retry unmount rejection",
                    mr_fs_umount("/unmount-reject"), 0);

    /* Mount across a covered directory and walk mount boundaries */
    fs_mount_expect(__test_status, "mkdir mount", mr_fs_mkdir("/mount"), 0);
    ret = mr_fs_open(&file, "/mount/covered", MR_O_RDWR | MR_O_CREAT);
    fs_mount_expect(__test_status, "create covered", ret, 0);
    if (ret == 0) {
        fs_mount_expect(__test_status, "close covered", mr_fs_close(&file), 0);
    }
    fs_mount_expect(__test_status, "mount nested root",
                    mr_fs_mount(MR_NULL, "/mount", "ramfs", 0, MR_NULL), 0);
    fs_mount_expect(__test_status, "repeat nested mount",
                    mr_fs_mount(MR_NULL, "/mount", "ramfs", 0, MR_NULL),
                    -MR_EBUSY);
    fs_mount_stat_expect(__test_status, "/mount/covered", -MR_ENOENT);
    fs_mount_expect(__test_status, "mkdir nested", mr_fs_mkdir("/mount/nested"),
                    0);
    fs_mount_expect(__test_status, "mount child",
                    mr_fs_mount(MR_NULL, "/mount/nested", "ramfs", 0, MR_NULL),
                    0);
    fs_mount_expect(__test_status, "umount busy parent", mr_fs_umount("/mount"),
                    -MR_EBUSY);
    fs_mount_expect(__test_status, "chdir child", mr_fs_chdir("/mount/nested"),
                    0);
    fs_mount_cwd_expect(__test_status, "/mount/nested");
    fs_mount_expect(__test_status, "chdir child parent", mr_fs_chdir(".."), 0);
    fs_mount_cwd_expect(__test_status, "/mount");
    fs_mount_expect(__test_status, "chdir mount parent", mr_fs_chdir("./.."),
                    0);
    fs_mount_cwd_expect(__test_status, "/");
    fs_mount_expect(__test_status, "umount child dot",
                    mr_fs_umount("/mount/nested/."), 0);
    fs_mount_expect(__test_status, "umount parent dot",
                    mr_fs_umount("/mount/."), 0);
    fs_mount_stat_expect(__test_status, "/mount/covered", 0);

    /* Build a chroot hierarchy and retain the original context for cleanup */
    fs_mount_expect(__test_status, "mkdir jail", mr_fs_mkdir("/jail"), 0);
    fs_mount_expect(__test_status, "mkdir jail work", mr_fs_mkdir("/jail/work"),
                    0);
    fs_mount_expect(__test_status, "mkdir outside", mr_fs_mkdir("/outside"), 0);
    ret = mr_fs_open(&file, "/jail/file", MR_O_RDWR | MR_O_CREAT);
    fs_mount_expect(__test_status, "create jail file", ret, 0);
    if (ret == 0) {
        fs_mount_expect(__test_status, "close jail file", mr_fs_close(&file),
                        0);
    }
    fs_mount_expect(__test_status, "chdir regular", mr_fs_chdir("/jail/file"),
                    -MR_ENOTDIR);
    fs_mount_expect(__test_status, "chroot regular", mr_fs_chroot("/jail/file"),
                    -MR_ENOTDIR);
    fs_mount_expect(__test_status, "create jail escape link",
                    mr_fs_symlink("/outside", "/jail/escape"), 0);
    fs_mount_expect(__test_status, "create jail work link",
                    mr_fs_symlink("work", "/jail/work-link"), 0);
    fs_mount_expect(__test_status, "chdir work link",
                    mr_fs_chdir("/jail/work-link"), 0);
    fs_mount_cwd_expect(__test_status, "/jail/work");
    fs_mount_expect(__test_status, "chdir outside", mr_fs_chdir("/outside"), 0);

    /* Pin the original context before changing the namespace root */
    nsp = mr_fs_nsp_current();
    saved_root = MR_NULL;
    saved_cwd = MR_NULL;
    if (!nsp) {
    } else {
        mask = mr_fs_nsp_lock();
        saved_root = mr_dentry_get(nsp->root);
        saved_cwd = mr_dentry_get(nsp->cwd);
        mr_fs_nsp_unlock(mask);
        if ((!saved_root) || (!saved_cwd)) {
        }
    }

    /* Constrain absolute paths and parent traversal below the chroot */
    if (saved_root && saved_cwd) {
        fs_mount_expect(__test_status, "chdir jail work",
                        mr_fs_chdir("/jail/work"), 0);
        fs_mount_expect(__test_status, "chroot retain cwd",
                        mr_fs_chroot("/jail"), 0);
        fs_mount_cwd_expect(__test_status, "/work");
        fs_mount_stat_expect(__test_status, "/escape", -MR_ENOENT);
        fs_mount_expect(__test_status, "chdir work parent", mr_fs_chdir(".."),
                        0);
        fs_mount_cwd_expect(__test_status, "/");
        fs_mount_restore_nsp(mr_dentry_get(saved_root),
                             mr_dentry_get(saved_cwd));
        fs_mount_expect(__test_status, "chdir outside again",
                        mr_fs_chdir("/outside"), 0);
        fs_mount_expect(__test_status, "chroot jail", mr_fs_chroot("/jail"), 0);
        fs_mount_cwd_expect(__test_status, "/");
        fs_mount_stat_expect(__test_status, "/work", 0);
        fs_mount_stat_expect(__test_status, "/outside", -MR_ENOENT);
        fs_mount_stat_expect(__test_status, "/../outside", -MR_ENOENT);
        fs_mount_expect(__test_status, "chdir work", mr_fs_chdir("/work"), 0);
        fs_mount_cwd_expect(__test_status, "/work");
        fs_mount_expect(__test_status, "chdir work dot", mr_fs_chdir("."), 0);
        fs_mount_cwd_expect(__test_status, "/work");
        fs_mount_expect(__test_status, "chdir root parent", mr_fs_chdir("/.."),
                        0);
        fs_mount_cwd_expect(__test_status, "/");
        fs_mount_expect(__test_status, "chroot work", mr_fs_chroot("/work"), 0);
        fs_mount_cwd_expect(__test_status, "/");
        fs_mount_expect(__test_status, "chdir nested parent", mr_fs_chdir(".."),
                        0);
        fs_mount_cwd_expect(__test_status, "/");

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
    fs_mount_expect(__test_status, "chdir root", mr_fs_chdir("/"), 0);

    /* Unmount while the current directory is inside the mount */
    fs_mount_expect(__test_status, "mkdir cwd mount", mr_fs_mkdir("/cwd-mnt"),
                    0);
    fs_mount_expect(__test_status, "mount cwd",
                    mr_fs_mount(MR_NULL, "/cwd-mnt", "ramfs", 0, MR_NULL), 0);
    fs_mount_expect(__test_status, "chdir cwd mount", mr_fs_chdir("/cwd-mnt"),
                    0);
    fs_mount_expect(__test_status, "umount cwd mount", mr_fs_umount("/cwd-mnt"),
                    0);
    {
        char detached[8];

        fs_mount_expect(__test_status, "getcwd detached",
                        (mr_err_t)mr_fs_getcwd(detached, sizeof(detached)),
                        -MR_ENOENT);
    }
    fs_mount_expect(__test_status, "chdir root after detach", mr_fs_chdir("/"),
                    0);
    fs_mount_expect(__test_status, "umount ramfs", mr_fs_umount("/"), 0);
    return;
}

MR_TEST(fs, mount) {
    fs_mount_test(__test_status);
}

#endif /* defined(MR_USE_TEST) && defined(MR_USE_FS) */
