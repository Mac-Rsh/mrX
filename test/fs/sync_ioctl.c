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

static int sync_ioctl_expect(const char *name, mr_int64_t actual,
                             mr_int64_t expected) {
    MR_IGNORE(name);

    MR_TEST_EXPECT_EQ_INT(actual, expected);
    return (actual == expected) ? 0 : 1;
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

static int fs_sync_ioctl_test(void) {
    mr_file_t file = MR_FILE_INIT(file);
    int arg = 7;
    int failures = 0;
    mr_err_t ret;

    /* RamFS has no sync/ioctl hooks */
    ret = mr_fs_mount(MR_NULL, "/", "ramfs", 0, MR_NULL);
    failures += sync_ioctl_expect("mount ramfs", ret, 0);
    if (ret != 0) {
        return failures;
    }

    ret = mr_fs_open(&file, "/nosync", MR_O_RDWR | MR_O_CREAT);
    failures += sync_ioctl_expect("create nosync", ret, 0);
    if (ret == 0) {
        failures += sync_ioctl_expect("ramfs sync", mr_fs_sync(&file),
                                      -MR_ENOSYS);
        failures += sync_ioctl_expect("ramfs ioctl",
                                      mr_fs_ioctl(&file, 1, MR_NULL),
                                      -MR_ENOSYS);
        failures += sync_ioctl_expect("close nosync", mr_fs_close(&file), 0);
    }
    failures += sync_ioctl_expect("umount ramfs", mr_fs_umount("/"), 0);

    /* Closed-handle rejection */
    failures += sync_ioctl_expect("sync closed", mr_fs_sync(&file), -MR_EBADF);
    failures += sync_ioctl_expect("ioctl closed",
                                  mr_fs_ioctl(&file, 1, MR_NULL), -MR_EBADF);

    /* Register and exercise a FS that implements both hooks */
    ret = mr_file_system_register(&__syncfs);
    if ((ret != 0) && (ret != -MR_EEXIST)) {
        failures += sync_ioctl_expect("register syncfs", ret, 0);
        return failures;
    }

    __sync_calls = 0;
    __ioctl_calls = 0;
    __last_ioctl_cmd = 0;
    __last_ioctl_arg = MR_NULL;

    ret = mr_fs_mount(MR_NULL, "/", "syncfs", 0, MR_NULL);
    failures += sync_ioctl_expect("mount syncfs", ret, 0);
    if (ret != 0) {
        return failures;
    }

    ret = mr_fs_open(&file, "/hooked", MR_O_RDWR | MR_O_CREAT);
    failures += sync_ioctl_expect("create hooked", ret, 0);
    if (ret == 0) {
        failures += sync_ioctl_expect("sync hooked", mr_fs_sync(&file), 0);
        failures += sync_ioctl_expect("sync call count",
                                      (mr_int64_t)__sync_calls, 1);
        failures += sync_ioctl_expect("ioctl good",
                                      mr_fs_ioctl(&file, 0x55, &arg), 0);
        failures += sync_ioctl_expect("ioctl call count",
                                      (mr_int64_t)__ioctl_calls, 1);
        failures += sync_ioctl_expect("ioctl cmd", __last_ioctl_cmd, 0x55);
        if (__last_ioctl_arg != &arg) {
            MR_TEST_EXPECT_EQ_PTR(__last_ioctl_arg, &arg);
            failures++;
        }
        failures += sync_ioctl_expect("ioctl bad cmd",
                                      mr_fs_ioctl(&file, 0, MR_NULL),
                                      -MR_EINVAL);
        failures += sync_ioctl_expect("close hooked", mr_fs_close(&file), 0);
    }

    failures += sync_ioctl_expect("umount syncfs", mr_fs_umount("/"), 0);
    return failures;
}

static void sync_ioctl(void) {
    MR_TEST_EXPECT_EQ_INT(fs_sync_ioctl_test(), 0);
}
MR_TEST_EXPORT(fs, sync_ioctl, sync_ioctl);
#endif /* defined(MR_USE_TEST) && defined(MR_USE_FS) */
