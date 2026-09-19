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
#include <mrX/fs/mr_fs_struct.h>
#include <mrX/test/fs/fs_test.h>

/* Failing filesystem mount state */
static mr_size_t __fs_mount_fail_calls;

static mr_err_t fs_mount_fail_mount(mr_sblock_t *sb, const char *source,
                                    int flags, void *data);

static mr_file_system_t __fs_mount_fail
    = MR_FILE_SYSTEM_INIT("mountfail", fs_mount_fail_mount, MR_NULL);

static void fs_mount_expect(volatile mr_uint32_t *__test_status,
                            const char *name, mr_int64_t actual,
                            mr_int64_t expected) {
    MR_IGNORE(name);

    MR_TEST_EXPECT_EQ_INT(actual, expected);
}

static void fs_mount_register(volatile mr_uint32_t *__test_status,
                              const char *name, mr_file_system_t *fs) {
    mr_err_t ret;

    /* Reuse the test-owned registration on repeated test runs */
    ret = mr_file_system_register(fs);
    if ((ret == -MR_EEXIST) && (mr_file_system_find(fs->name) == fs)) {
        ret = 0;
    }
    fs_mount_expect(__test_status, name, ret, 0);
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

static mr_err_t fs_mount_fail_mount(mr_sblock_t *sb, const char *source,
                                    int flags, void *data) {
    MR_IGNORE(sb);
    MR_IGNORE(source);
    MR_IGNORE(flags);
    MR_IGNORE(data);

    /* Record the callback before rejecting the mount */
    __fs_mount_fail_calls++;
    return -MR_ENOMEM;
}

static void fs_mount_restore_struct(mr_dentry_t *root, mr_dentry_t *cwd) {
    mr_fs_struct_t *fs_struct;
    mr_dentry_t *old_root, *old_cwd;
    int mask;

    /* Get current file-system namespace */
    fs_struct = mr_fs_struct_current();
    if (!fs_struct) {
        return;
    }

    /* Restore saved context */
    mask = mr_fs_struct_lock();
    old_root = fs_struct->root;
    old_cwd = fs_struct->cwd;
    fs_struct->root = root;
    fs_struct->cwd = cwd;
    mr_fs_struct_unlock(mask);

    /* Drop replaced namespace references */
    mr_dentry_put(old_root);
    mr_dentry_put(old_cwd);
}

MR_TEST(fs, mount) {
    mr_fs_struct_t *fs_struct;
    mr_dentry_t *saved_root, *saved_cwd;
    mr_file_t file = MR_FILE_INIT(file);
    int mask;
    mr_err_t ret;

    /* Register a filesystem which rejects mount requests */
    fs_mount_register(__test_status, "register mountfail", &__fs_mount_fail);

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
    fs_mount_expect(__test_status, "chdir jail work",
                    mr_fs_chdir("/jail/work"), 0);
    fs_mount_cwd_expect(__test_status, "/jail/work");
    fs_mount_expect(__test_status, "chdir outside", mr_fs_chdir("/outside"), 0);

    /* Pin the original context before changing the namespace root */
    fs_struct = mr_fs_struct_current();
    saved_root = MR_NULL;
    saved_cwd = MR_NULL;
    if (!fs_struct) {
    } else {
        mask = mr_fs_struct_lock();
        saved_root = mr_dentry_get(fs_struct->root);
        saved_cwd = mr_dentry_get(fs_struct->cwd);
        mr_fs_struct_unlock(mask);
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
        fs_mount_expect(__test_status, "chdir work parent", mr_fs_chdir(".."),
                        0);
        fs_mount_cwd_expect(__test_status, "/");
        fs_mount_restore_struct(mr_dentry_get(saved_root),
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
        fs_mount_restore_struct(saved_root, saved_cwd);
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
/* ==== from umount_open.c ==== */
static void umount_open_expect(volatile mr_uint32_t *__test_status,
                               const char *name, mr_int64_t actual,
                               mr_int64_t expected) {
    MR_IGNORE(name);

    MR_TEST_EXPECT_EQ_INT(actual, expected);
}

MR_TEST(fs, umount_open) {
    mr_file_t file = MR_FILE_INIT(file);
    mr_file_t dir = MR_FILE_INIT(dir);
    mr_stat_t stat;
    char buffer[8];
    mr_ssize_t io_ret;
    mr_err_t ret;

    /* Nested mount so root can stay free for other tests' assumptions */
    ret = fs_test_mount_root("ramfs", 0, MR_NULL);
    umount_open_expect(__test_status, "mount root", ret, 0);
    if (ret != 0) {
        return;
    }

    umount_open_expect(__test_status, "mkdir nested", mr_fs_mkdir("/nested"),
                       0);
    umount_open_expect(__test_status, "mount nested",
                       mr_fs_mount(MR_NULL, "/nested", "ramfs", 0, MR_NULL), 0);

    /* An open file blocks graceful unmount */
    ret = mr_fs_open(&file, "/nested/open-file", MR_O_RDWR | MR_O_CREAT);
    umount_open_expect(__test_status, "create open-file", ret, 0);
    if (ret != 0) {
        goto _exit;
    }
    umount_open_expect(__test_status, "write open-file",
                       mr_fs_write(&file, "data", 4), 4);

    /* Graceful unmount requires every file to be closed */
    umount_open_expect(__test_status, "umount nested open",
                       mr_fs_umount("/nested"), -MR_EBUSY);

    /* The mount and open handle remain usable after the rejected request */
    umount_open_expect(__test_status, "path after busy umount",
                       fs_test_stat("/nested/open-file", &stat), 0);

    umount_open_expect(__test_status, "stat open after busy umount",
                       mr_fs_fstat(&file, &stat), 0);
    umount_open_expect(__test_status, "size after busy umount", stat.size, 4);
    umount_open_expect(__test_status, "seek after busy umount",
                       mr_fs_lseek(&file, 0, MR_SEEK_SET), 0);
    io_ret = mr_fs_read(&file, buffer, sizeof(buffer));
    umount_open_expect(__test_status, "read after busy umount", io_ret, 4);
    if ((io_ret == 4) && (mr_memcmp(buffer, "data", 4) != 0)) {
        MR_TEST_EXPECT_EQ_INT(mr_memcmp(buffer, "data", 4), 0);
    }
    umount_open_expect(__test_status, "write after busy umount",
                       mr_fs_write(&file, "!", 1), 1);
    umount_open_expect(__test_status, "close before umount", mr_fs_close(&file),
                       0);
    umount_open_expect(__test_status, "umount after close",
                       mr_fs_umount("/nested"), 0);

    /* An open directory also blocks graceful unmount */
    umount_open_expect(__test_status, "remount nested",
                       mr_fs_mount(MR_NULL, "/nested", "ramfs", 0, MR_NULL), 0);
    umount_open_expect(__test_status, "mkdir nested/d",
                       mr_fs_mkdir("/nested/d"), 0);
    ret = mr_fs_open(&dir, "/nested/d", MR_O_RDONLY | MR_O_DIRECTORY);
    umount_open_expect(__test_status, "opendir nested/d", ret, 0);
    if (ret == 0) {
        umount_open_expect(__test_status, "umount with opendir",
                           mr_fs_umount("/nested"), -MR_EBUSY);
        umount_open_expect(__test_status, "closedir before umount",
                           mr_fs_close(&dir), 0);
        umount_open_expect(__test_status, "umount after closedir",
                           mr_fs_umount("/nested"), 0);
    }

    /* Active op_cnt blocks umount of that super-block */
    umount_open_expect(__test_status, "remount for op_cnt",
                       mr_fs_mount(MR_NULL, "/nested", "ramfs", 0, MR_NULL), 0);
    ret = mr_fs_open(&file, "/nested/hold", MR_O_RDWR | MR_O_CREAT);
    umount_open_expect(__test_status, "create hold", ret, 0);
    if (ret == 0) {
        mr_sblock_t *sb = file.inode ? file.inode->sb : MR_NULL;

        if (sb) {
            umount_open_expect(__test_status, "op enter",
                               mr_sblock_op_enter(sb), 0);
            umount_open_expect(__test_status, "umount busy op",
                               mr_fs_umount("/nested"), -MR_EBUSY);
            mr_sblock_op_exit(sb);
            umount_open_expect(__test_status, "umount busy open",
                               mr_fs_umount("/nested"), -MR_EBUSY);
        }
        umount_open_expect(__test_status, "close hold", mr_fs_close(&file), 0);
        umount_open_expect(__test_status, "umount after op and close",
                           mr_fs_umount("/nested"), 0);
    }

_exit:
    umount_open_expect(__test_status, "umount root", mr_fs_umount("/"), 0);
    return;
}
/* ==== from sb.c ==== */
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
/* ==== from sblock_ops.c ==== */
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
/* ==== from registry.c ==== */
/* Mock FS that returns a non-directory root from mount */
static mr_err_t registry_badroot_mount(mr_sblock_t *sb, const char *source,
                                       int flags, void *data);
static mr_err_t registry_good_mount(mr_sblock_t *sb, const char *source,
                                    int flags, void *data);
static void registry_good_unmount(mr_sblock_t *sb);

static mr_file_system_t __registry_badroot_fs = MR_FILE_SYSTEM_INIT(
    "badrootfs", registry_badroot_mount, MR_NULL);
static mr_file_system_t __registry_good_fs = MR_FILE_SYSTEM_INIT(
    "registryfs", registry_good_mount, registry_good_unmount);

static mr_size_t __registry_unmount_calls;

static void registry_expect(volatile mr_uint32_t *__test_status,
                            const char *name, mr_int64_t actual,
                            mr_int64_t expected) {
    MR_IGNORE(name);

    MR_TEST_EXPECT_EQ_INT(actual, expected);
}

static mr_err_t registry_badroot_mount(mr_sblock_t *sb, const char *source,
                                       int flags, void *data) {
    MR_IGNORE(source);
    MR_IGNORE(flags);
    MR_IGNORE(data);

    sb->root = mr_inode_create(MR_INODE_REG, MR_NULL, MR_NULL, MR_NULL);
    return sb->root ? 0 : -MR_ENOMEM;
}

static mr_err_t registry_good_mount(mr_sblock_t *sb, const char *source,
                                    int flags, void *data) {
    MR_IGNORE(source);
    MR_IGNORE(flags);
    MR_IGNORE(data);

    sb->root = mr_inode_create(MR_INODE_DIR, MR_NULL, MR_NULL, MR_NULL);
    return sb->root ? 0 : -MR_ENOMEM;
}

static void registry_good_unmount(mr_sblock_t *sb) {
    MR_IGNORE(sb);

    __registry_unmount_calls++;
}

MR_TEST(fs, registry) {
    mr_file_system_t *found;
    mr_dentry_t *dentry;
    mr_inode_t *inode;
    mr_err_t ret;

    /* Register the mock file-systems */
    ret = mr_file_system_register(&__registry_badroot_fs);
    registry_expect(__test_status, "register badrootfs", ret, 0);
    ret = mr_file_system_register(&__registry_good_fs);
    registry_expect(__test_status, "register registryfs", ret, 0);

    /* Duplicate name or pointer is rejected */
    ret = mr_file_system_register(&__registry_good_fs);
    registry_expect(__test_status, "duplicate pointer", ret, -MR_EEXIST);
    ret = mr_file_system_register(&__registry_badroot_fs);
    registry_expect(__test_status, "duplicate name", ret, -MR_EEXIST);

    /* Linear registration: name lookup */
    found = mr_file_system_find("registryfs");
    registry_expect(__test_status, "find registryfs", found != MR_NULL,
                    MR_TRUE);
    found = mr_file_system_find("no-such-fs");
    registry_expect(__test_status, "find missing", found == MR_NULL, MR_TRUE);

    /* A mount whose root inode is not a directory raises -ENOTDIR */
    ret = fs_test_mount_root("badrootfs", 0, MR_NULL);
    registry_expect(__test_status, "mount badrootfs", ret, -MR_ENOTDIR);

    /* A well-formed mount releases the backend on unmount */
    __registry_unmount_calls = 0;
    ret = fs_test_mount_root("registryfs", 0, MR_NULL);
    registry_expect(__test_status, "mount registryfs", ret, 0);
    if (ret != 0) {
        return;
    }
    registry_expect(__test_status, "unmount registryfs", mr_fs_umount("/"), 0);
    registry_expect(__test_status, "unmount called",
                    (mr_int64_t)__registry_unmount_calls, 1);

    /* Static dentry lifecycle */
    dentry = mr_malloc(sizeof(mr_dentry_t));
    if (dentry) {
        ret = mr_dentry_init(dentry, MR_NULL, "%s", "static-name");
        registry_expect(__test_status, "static dentry init", ret, 0);
        if (ret == 0) {
            registry_expect(__test_status, "static dentry del",
                            mr_dentry_del(dentry), 0);
        }
        mr_free(dentry);
    }

    /* Static inode lifecycle */
    inode = mr_malloc(sizeof(mr_inode_t));
    if (inode) {
        ret = mr_inode_init(inode, MR_INODE_REG, MR_NULL, MR_NULL, MR_NULL);
        registry_expect(__test_status, "static inode init", ret, 0);
        if (ret == 0) {
            mr_inode_put(inode);
        }
        mr_free(inode);
    }

    return;
}
/* ==== from media_detach.c ==== */
static void media_detach_expect(volatile mr_uint32_t *__test_status,
                                const char *name, mr_int64_t actual,
                                mr_int64_t expected) {
    MR_IGNORE(name);

    MR_TEST_EXPECT_EQ_INT(actual, expected);
}

MR_TEST(fs, media_detach) {
    mr_file_t file = MR_FILE_INIT(file);
    mr_stat_t stat;
    char buffer[4];
    mr_err_t ret;

    /* Mount an independent child file system */
    ret = fs_test_mount_root("ramfs", 0, MR_NULL);
    media_detach_expect(__test_status, "mount root", ret, 0);
    if (ret != 0) {
        return;
    }
    media_detach_expect(__test_status, "mkdir media", mr_fs_mkdir("/media"),
                        0);
    media_detach_expect(__test_status, "mount media",
                        mr_fs_mount(MR_NULL, "/media", "ramfs", 0, MR_NULL),
                        0);

    /* Keep a handle alive while the child mount disappears */
    ret = mr_fs_open(&file, "/media/live", MR_O_RDWR | MR_O_CREAT);
    media_detach_expect(__test_status, "open live file", ret, 0);
    if (ret != 0) {
        goto _exit;
    }
    media_detach_expect(__test_status, "write live file",
                        mr_fs_write(&file, "ok", 2), 2);

    /* Media loss hides the child mount and invalidates existing handles */
    media_detach_expect(__test_status, "detach media",
                        mr_fs_media_detach("/media"), 0);
    media_detach_expect(__test_status, "hidden child content",
                        fs_test_stat("/media/live", &stat), -MR_ENOENT);
    media_detach_expect(__test_status, "offline fstat",
                        mr_fs_fstat(&file, &stat), -MR_ENODEV);
    media_detach_expect(__test_status, "offline read",
                        mr_fs_read(&file, buffer, sizeof(buffer)), -MR_ENODEV);
    media_detach_expect(__test_status, "offline write",
                        mr_fs_write(&file, "!", 1), -MR_ENODEV);
    media_detach_expect(__test_status, "offline close", mr_fs_close(&file),
                        -MR_ENODEV);
    media_detach_expect(__test_status, "detached mount absent",
                        mr_fs_umount("/media"), -MR_ENOENT);

_exit:
    if (file.inode) {
        media_detach_expect(__test_status, "close remaining file",
                            mr_fs_close(&file), -MR_ENODEV);
    }
    media_detach_expect(__test_status, "umount root", mr_fs_umount("/"), 0);
}

#endif /* defined(MR_USE_TEST) && defined(MR_USE_FS) */
