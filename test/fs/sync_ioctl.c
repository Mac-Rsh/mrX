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
#include "fs_test.h"

/* Mock FS with sync/ioctl hooks */
static mr_size_t __sync_calls;
static mr_size_t __ioctl_calls;
static int __last_ioctl_cmd;
static void *__last_ioctl_arg;

static mr_err_t syncfs_inode_create(mr_inode_t *dir, const char *name,
                                    mr_inode_type_t type, mr_inode_t **out);
static mr_err_t syncfs_file_sync(mr_file_t *file);
static mr_err_t syncfs_file_ioctl(mr_file_t *file, int cmd, void *arg);
static mr_err_t syncfs_mount(mr_super_block_t *sb, const char *source,
                             int flags, void *data);

static const mr_inode_ops_t __syncfs_inode_ops = {
    .create = syncfs_inode_create,
};

static const mr_file_ops_t __syncfs_file_ops = {
    .sync = syncfs_file_sync,
    .ioctl = syncfs_file_ioctl,
};

static mr_file_system_t __syncfs
    = MR_FILE_SYSTEM_INIT("syncfs", syncfs_mount, MR_NULL, MR_NULL);

static void sync_ioctl_expect(volatile mr_uint32_t *__test_status,
                              const char *name, mr_int64_t actual,
                              mr_int64_t expected) {
    MR_IGNORE(name);

    MR_TEST_EXPECT_EQ_INT(actual, expected);
}

static mr_err_t syncfs_inode_create(mr_inode_t *dir, const char *name,
                                    mr_inode_type_t type, mr_inode_t **out) {
    MR_IGNORE(dir);
    MR_IGNORE(name);

    *out = mr_inode_create(type, &__syncfs_inode_ops, &__syncfs_file_ops,
                           MR_NULL);
    return (*out) ? 0 : -MR_ENOMEM;
}

static mr_err_t syncfs_file_sync(mr_file_t *file) {
    MR_IGNORE(file);

    __sync_calls++;
    return 0;
}

static mr_err_t syncfs_file_ioctl(mr_file_t *file, int cmd, void *arg) {
    MR_IGNORE(file);

    __ioctl_calls++;
    __last_ioctl_cmd = cmd;
    __last_ioctl_arg = arg;
    return (cmd == 0x55) ? 0 : -MR_EINVAL;
}

static mr_err_t syncfs_mount(mr_super_block_t *sb, const char *source,
                             int flags, void *data) {
    MR_IGNORE(source);
    MR_IGNORE(flags);
    MR_IGNORE(data);

    sb->root = mr_inode_create(MR_INODE_DIR, &__syncfs_inode_ops,
                               &__syncfs_file_ops, MR_NULL);
    return sb->root ? 0 : -MR_ENOMEM;
}

static void fs_sync_ioctl_test(volatile mr_uint32_t *__test_status) {
    mr_file_t file = MR_FILE_INIT(file);
    int arg = 7;
    mr_err_t ret;

    /* RamFS has no sync/ioctl hooks */
    ret = fs_test_mount_root("ramfs", 0, MR_NULL);
    sync_ioctl_expect(__test_status, "mount ramfs", ret, 0);
    if (ret != 0) {
        return;
    }

    ret = mr_fs_open(&file, "/nosync", MR_O_RDWR | MR_O_CREAT);
    sync_ioctl_expect(__test_status, "create nosync", ret, 0);
    if (ret == 0) {
        sync_ioctl_expect(__test_status, "ramfs sync", mr_fs_sync(&file),
                          -MR_ENOSYS);
        sync_ioctl_expect(__test_status, "ramfs ioctl",
                          mr_fs_ioctl(&file, 1, MR_NULL), -MR_ENOSYS);
        sync_ioctl_expect(__test_status, "close nosync", mr_fs_close(&file), 0);
    }
    sync_ioctl_expect(__test_status, "umount ramfs", mr_fs_umount("/"), 0);

    /* Closed-handle rejection */
    sync_ioctl_expect(__test_status, "sync closed", mr_fs_sync(&file),
                      -MR_EBADF);
    sync_ioctl_expect(__test_status, "ioctl closed",
                      mr_fs_ioctl(&file, 1, MR_NULL), -MR_EBADF);

    /* Register and exercise a FS that implements both hooks */
    ret = mr_file_system_register(&__syncfs);
    if ((ret != 0) && (ret != -MR_EEXIST)) {
        sync_ioctl_expect(__test_status, "register syncfs", ret, 0);
        return;
    }

    __sync_calls = 0;
    __ioctl_calls = 0;
    __last_ioctl_cmd = 0;
    __last_ioctl_arg = MR_NULL;

    ret = fs_test_mount_root("syncfs", 0, MR_NULL);
    sync_ioctl_expect(__test_status, "mount syncfs", ret, 0);
    if (ret != 0) {
        return;
    }

    ret = mr_fs_open(&file, "/hooked", MR_O_RDWR | MR_O_CREAT);
    sync_ioctl_expect(__test_status, "create hooked", ret, 0);
    if (ret == 0) {
        sync_ioctl_expect(__test_status, "sync hooked", mr_fs_sync(&file), 0);
        sync_ioctl_expect(__test_status, "sync call count",
                          (mr_int64_t)__sync_calls, 1);
        sync_ioctl_expect(__test_status, "ioctl good",
                          mr_fs_ioctl(&file, 0x55, &arg), 0);
        sync_ioctl_expect(__test_status, "ioctl call count",
                          (mr_int64_t)__ioctl_calls, 1);
        sync_ioctl_expect(__test_status, "ioctl cmd", __last_ioctl_cmd, 0x55);
        if (__last_ioctl_arg != &arg) {
            MR_TEST_EXPECT_EQ_PTR(__last_ioctl_arg, &arg);
        }
        sync_ioctl_expect(__test_status, "ioctl bad cmd",
                          mr_fs_ioctl(&file, 0, MR_NULL), -MR_EINVAL);
        sync_ioctl_expect(__test_status, "close hooked", mr_fs_close(&file), 0);
    }

    sync_ioctl_expect(__test_status, "umount syncfs", mr_fs_umount("/"), 0);
    return;
}

MR_TEST(fs, sync_ioctl) {
    fs_sync_ioctl_test(__test_status);
}

#endif /* defined(MR_USE_TEST) && defined(MR_USE_FS) */
