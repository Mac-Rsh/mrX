/**
 * @copyright (c) 2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2026-07-20        MacRsh        First version
 */

#include <mrX/test/mr_test.h>
#if defined(MR_USE_TEST) && defined(MR_USE_FS)
#include <mrX.h>
#include <mrX/test/fs/fs_test.h>
#include <mrX/fs/mr_fs_dentry.h>
#include <mrX/fs/mr_fs_struct.h>

static void rootfs_expect(volatile mr_uint32_t *__test_status, const char *name,
                          mr_int64_t actual, mr_int64_t expected) {
    MR_IGNORE(name);

    MR_TEST_EXPECT_EQ_INT(actual, expected);
}

static void rootfs_stat_expect(volatile mr_uint32_t *__test_status,
                               const char *path, mr_err_t expected,
                               mr_inode_type_t type) {
    mr_stat_t stat;
    mr_err_t ret;

    ret = fs_test_stat(path, &stat);
    rootfs_expect(__test_status, path, ret, expected);
    if ((ret == 0) && (stat.type != type)) {
        MR_TEST_EXPECT_EQ_INT(stat.type, type);
    }
    return;
}

static void rootfs_directory_expect(volatile mr_uint32_t *__test_status) {
    mr_file_t directory = MR_FILE_INIT(directory);
    mr_bool_t found_services = MR_FALSE;
    mr_bool_t found_console = MR_FALSE;
    mr_bool_t found_config = MR_FALSE;
    mr_dirent_t dirent;
    mr_err_t ret;

    /* Open the directory */
    ret = mr_fs_open(&directory, "/system", MR_O_RDONLY | MR_O_DIRECTORY);
    rootfs_expect(__test_status, "open rootfs directory", ret, 0);
    if (ret != 0) {
        return;
    }

    /* Enumerate structural nodes */
    while ((ret = mr_fs_readdir(&directory, &dirent)) > 0) {
        if ((mr_strcmp(dirent.name, "services") == 0)
            && (dirent.type == MR_INODE_DIR)) {
            found_services = MR_TRUE;
        } else if ((mr_strcmp(dirent.name, "console") == 0)
                   && (dirent.type == MR_INODE_CHR)) {
            found_console = MR_TRUE;
        } else if ((mr_strcmp(dirent.name, "config") == 0)
                   && (dirent.type == MR_INODE_REG)) {
            found_config = MR_TRUE;
        }
    }
    rootfs_expect(__test_status, "finish rootfs readdir", ret, 0);
    rootfs_expect(__test_status, "find rootfs services", found_services,
                  MR_TRUE);
    rootfs_expect(__test_status, "find rootfs console", found_console, MR_TRUE);
    rootfs_expect(__test_status, "find rootfs config", found_config, MR_TRUE);
    rootfs_expect(__test_status, "close rootfs directory",
                  mr_fs_close(&directory), 0);
    return;
}

MR_TEST(fs, rootfs) {
    mr_file_t file = MR_FILE_INIT(file);
    mr_heap_info_t heap_before;
    mr_heap_info_t heap_after;
    char buffer[4];
    int metadata;
    mr_err_t ret;

    /* Record heap state and mount RootFS */
    heap_before = mr_heap_information(MR_TRUE);
    ret = fs_test_mount_root("rootfs", 0, MR_NULL);
    rootfs_expect(__test_status, "mount rootfs", ret, 0);
    if (ret != 0) {
        return;
    }

    /* Build a namespace tree */
    rootfs_expect(__test_status, "mkdir rootfs system", mr_fs_mkdir("/system"),
                  0);
    rootfs_expect(__test_status, "mkdir rootfs services",
                  mr_fs_mkdir("/system/services"), 0);
    rootfs_expect(__test_status, "mknod rootfs config",
                  mr_fs_mknod("/system/config", MR_INODE_REG, &metadata), 0);
    rootfs_expect(__test_status, "mknod rootfs console",
                  mr_fs_mknod("/system/console", MR_INODE_CHR, MR_NULL), 0);
    rootfs_stat_expect(__test_status, "/system", 0, MR_INODE_DIR);
    rootfs_stat_expect(__test_status, "/system/services", 0, MR_INODE_DIR);
    rootfs_stat_expect(__test_status, "/system/config", 0, MR_INODE_REG);
    rootfs_stat_expect(__test_status, "/system/console", 0, MR_INODE_CHR);
    rootfs_directory_expect(__test_status);

    /* Nodes can be opened, but contain no file data */
    ret = mr_fs_open(&file, "/system/config", MR_O_RDWR);
    rootfs_expect(__test_status, "open rootfs node", ret, 0);
    if (ret == 0) {
        rootfs_expect(__test_status, "rootfs node private data",
                      file.inode->priv == &metadata, MR_TRUE);
        rootfs_expect(__test_status, "reject rootfs read",
                      mr_fs_read(&file, buffer, sizeof(buffer)), -MR_ENOSYS);
        rootfs_expect(__test_status, "reject rootfs write",
                      mr_fs_write(&file, buffer, sizeof(buffer)), -MR_ENOSYS);
        rootfs_expect(__test_status, "reject rootfs truncate",
                      mr_fs_ftruncate(&file, 0), -MR_ENOSYS);
        rootfs_expect(__test_status, "close rootfs node", mr_fs_close(&file),
                      0);
    }

    rootfs_expect(__test_status, "mknod rootfs directory",
                  mr_fs_mknod("/system/devices", MR_INODE_DIR, MR_NULL), 0);
    rootfs_stat_expect(__test_status, "/system/devices", 0, MR_INODE_DIR);

    /* Nested RootFS can be mounted and unmounted */
    rootfs_expect(__test_status, "mkdir rootfs mount",
                  mr_fs_mkdir("/system/mnt"), 0);
    rootfs_expect(__test_status, "mount nested rootfs",
                  mr_fs_mount(MR_NULL, "/system/mnt", "rootfs", 0, MR_NULL), 0);
    rootfs_expect(__test_status, "mkdir nested rootfs",
                  mr_fs_mkdir("/system/mnt/child"), 0);
    rootfs_stat_expect(__test_status, "/system/mnt/child", 0, MR_INODE_DIR);
    rootfs_expect(__test_status, "umount nested rootfs",
                  mr_fs_umount("/system/mnt"), 0);

    /* Generic file creation is not a RootFS operation */
    rootfs_expect(__test_status, "reject rootfs file create",
                  mr_fs_open(&file, "/created", MR_O_RDWR | MR_O_CREAT),
                  -MR_ENOSYS);
    rootfs_stat_expect(__test_status, "/created", -MR_ENOENT, MR_INODE_NONE);
    rootfs_expect(__test_status, "reject rootfs truncate open",
                  mr_fs_open(&file, "/system/config", MR_O_WRONLY | MR_O_TRUNC),
                  -MR_ENOSYS);

    /* Rename and remove structural nodes */
    rootfs_expect(__test_status, "rmdir rootfs devices",
                  mr_fs_rmdir("/system/devices"), 0);
    rootfs_expect(__test_status, "rmdir rootfs mount point",
                  mr_fs_rmdir("/system/mnt"), 0);
    rootfs_expect(__test_status, "rename rootfs node",
                  mr_fs_rename("/system/config", "/system/settings"), 0);
    rootfs_stat_expect(__test_status, "/system/config", -MR_ENOENT,
                       MR_INODE_NONE);
    rootfs_stat_expect(__test_status, "/system/settings", 0, MR_INODE_REG);
    rootfs_expect(__test_status, "reject nonempty rootfs directory",
                  mr_fs_rmdir("/system"), -MR_ENOTEMPTY);
    rootfs_expect(__test_status, "unlink rootfs settings",
                  mr_fs_unlink("/system/settings"), 0);
    rootfs_expect(__test_status, "unlink rootfs console",
                  mr_fs_unlink("/system/console"), 0);
    rootfs_expect(__test_status, "rmdir rootfs services",
                  mr_fs_rmdir("/system/services"), 0);
    rootfs_expect(__test_status, "rmdir rootfs system",
                  mr_fs_rmdir("/system"), 0);

    /* Unmount and verify all namespace allocations were released */
    rootfs_expect(__test_status, "umount rootfs", mr_fs_umount("/"), 0);
    heap_after = mr_heap_information(MR_TRUE);
    rootfs_expect(__test_status, "rootfs heap restored",
                  heap_after.used_bytes == heap_before.used_bytes, MR_TRUE);
    return;
}
/* ==== from nsp.c ==== */
MR_TEST(fs, nsp_init_root_get_failure) {
    mr_fs_struct_t *current;
    mr_fs_struct_t fs_struct = {0};
    mr_atomic_t saved;
    mr_err_t ret;

    /* Get the active namespace and preserve the root reference */
    current = mr_fs_struct_current();
    MR_TEST_ASSERT_NE_PTR(current, MR_NULL);
    MR_TEST_ASSERT_NE_PTR(current->root, MR_NULL);
    saved = mr_atomic_load(&current->root->parent.magic_ref);

    /* Force the first root reference acquisition to fail */
    mr_atomic_store(&current->root->parent.magic_ref, MR_REF_MAGIC);
    ret = mr_fs_struct_init(&fs_struct);
    MR_TEST_ASSERT_EQ_INT(ret, -MR_EINVAL);
    MR_TEST_ASSERT_EQ_PTR(fs_struct.root, MR_NULL);
    MR_TEST_ASSERT_EQ_PTR(fs_struct.cwd, MR_NULL);

    /* Restore the active namespace root reference */
    mr_atomic_store(&current->root->parent.magic_ref, saved);
}

MR_TEST(fs, nsp_root_lock_and_init) {
    mr_dentry_t *root;
    mr_fs_struct_t *current;
    mr_fs_struct_t fs_struct = {0};
    mr_err_t ret;
    int mask;

    root = mr_fs_struct_root();
    MR_TEST_ASSERT_NE_PTR(root, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(mr_dentry_is_root(root), MR_TRUE);
    MR_TEST_ASSERT_EQ_INT(mr_dentry_is_root(MR_NULL), MR_FALSE);

    current = mr_fs_struct_current();
    MR_TEST_ASSERT_NE_PTR(current, MR_NULL);
    MR_TEST_ASSERT_EQ_PTR(current->root, root);

    mask = mr_fs_struct_lock();
    mr_fs_struct_unlock(mask);

    ret = mr_fs_struct_init(&fs_struct);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_PTR(fs_struct.root, root);
    MR_TEST_ASSERT_EQ_PTR(fs_struct.cwd, root);
    if (fs_struct.root != MR_NULL) {
        mr_dentry_put(fs_struct.root);
    }
    if (fs_struct.cwd != MR_NULL) {
        mr_dentry_put(fs_struct.cwd);
    }
}

#endif /* defined(MR_USE_TEST) && defined(MR_USE_FS) */
