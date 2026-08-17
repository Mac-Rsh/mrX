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
#include "fs_test.h"

static int rootfs_expect(const char *name, mr_int64_t actual,
                         mr_int64_t expected) {
    MR_IGNORE(name);

    MR_TEST_EXPECT_EQ_INT(actual, expected);
    return (actual == expected) ? 0 : 1;
}

static int rootfs_stat_expect(const char *path, mr_err_t expected,
                              mr_inode_type_t type) {
    mr_stat_t stat;
    mr_err_t ret;
    int failures;

    ret = fs_test_stat(path, &stat);
    failures = rootfs_expect(path, ret, expected);
    if ((ret == 0) && (stat.type != type)) {
        MR_TEST_EXPECT_EQ_INT(stat.type, type);
        failures++;
    }
    return failures;
}

static int rootfs_directory_expect(void) {
    mr_file_t directory = MR_FILE_INIT(directory);
    mr_bool_t found_services = MR_FALSE;
    mr_bool_t found_console = MR_FALSE;
    mr_bool_t found_config = MR_FALSE;
    mr_dirent_t dirent;
    int failures = 0;
    mr_err_t ret;

    /* Open the directory */
    ret = mr_fs_opendir(&directory, "/system");
    failures += rootfs_expect("open rootfs directory", ret, 0);
    if (ret != 0) {
        return failures;
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
    failures += rootfs_expect("finish rootfs readdir", ret, 0);
    failures += rootfs_expect("find rootfs services", found_services, MR_TRUE);
    failures += rootfs_expect("find rootfs console", found_console, MR_TRUE);
    failures += rootfs_expect("find rootfs config", found_config, MR_TRUE);
    failures += rootfs_expect("close rootfs directory",
                               mr_fs_closedir(&directory), 0);
    return failures;
}

static int fs_rootfs_test(void) {
    mr_file_t file = MR_FILE_INIT(file);
    mr_heap_info_t heap_before;
    mr_heap_info_t heap_after;
    char buffer[4];
    int metadata;
    int failures = 0;
    mr_err_t ret;

    /* Record heap state and mount RootFS */
    heap_before = mr_heap_information(MR_TRUE);
    ret = mr_fs_mount(MR_NULL, "/", "rootfs", 0, MR_NULL);
    failures += rootfs_expect("mount rootfs", ret, 0);
    if (ret != 0) {
        return failures;
    }

    /* Build a namespace tree */
    failures += rootfs_expect("mkdir rootfs system", mr_fs_mkdir("/system"), 0);
    failures += rootfs_expect("mkdir rootfs services",
                              mr_fs_mkdir("/system/services"), 0);
    failures += rootfs_expect(
        "mknod rootfs config",
        mr_fs_mknod("/system/config", MR_INODE_REG, &metadata), 0);
    failures += rootfs_expect(
        "mknod rootfs console",
        mr_fs_mknod("/system/console", MR_INODE_CHR, MR_NULL), 0);
    failures += rootfs_stat_expect("/system", 0, MR_INODE_DIR);
    failures += rootfs_stat_expect("/system/services", 0, MR_INODE_DIR);
    failures += rootfs_stat_expect("/system/config", 0, MR_INODE_REG);
    failures += rootfs_stat_expect("/system/console", 0, MR_INODE_CHR);
    failures += rootfs_directory_expect();

    /* Nodes can be opened, but contain no file data */
    ret = mr_fs_open(&file, "/system/config", MR_O_RDWR);
    failures += rootfs_expect("open rootfs node", ret, 0);
    if (ret == 0) {
        failures += rootfs_expect("rootfs node private data",
                                  file.inode->priv == &metadata, MR_TRUE);
        failures += rootfs_expect("reject rootfs read",
                                   mr_fs_read(&file, buffer, sizeof(buffer)),
                                  -MR_ENOSYS);
        failures += rootfs_expect("reject rootfs write",
                                   mr_fs_write(&file, buffer, sizeof(buffer)),
                                  -MR_ENOSYS);
        failures += rootfs_expect("reject rootfs truncate",
                                   mr_fs_truncate(&file, 0), -MR_ENOSYS);
        failures += rootfs_expect("close rootfs node", mr_fs_close(&file), 0);
    }

    /* Built-in symbolic links and hard links work without RootFS hooks */
    failures += rootfs_expect(
        "create rootfs symlink",
        mr_fs_symlink("/system/config", "/system/config-link"), 0);
    ret = mr_fs_open(&file, "/system/config-link",
                     MR_O_RDONLY | MR_O_NOFOLLOW);
    failures += rootfs_expect("open rootfs symlink", ret, 0);
    if (ret == 0) {
        char target[16];

        failures += rootfs_expect(
            "readlink rootfs",
            mr_fs_readlink(&file, target, sizeof(target)), 14);
        if (mr_memcmp(target, "/system/config", 14) != 0) {
            MR_TEST_EXPECT_EQ_INT(mr_memcmp(target, "/system/config", 14),
                                  0);
            failures++;
        }
        failures += rootfs_expect("close rootfs symlink",
                                  mr_fs_close(&file), 0);
    }
    ret = mr_fs_open(&file, "/system/config", MR_O_RDONLY);
    failures += rootfs_expect("open rootfs link source", ret, 0);
    if (ret == 0) {
        failures += rootfs_expect(
            "hard-link rootfs node",
            mr_fs_link(&file, "/system/config-hard"), 0);
        failures += rootfs_expect("close rootfs link source",
                                  mr_fs_close(&file), 0);
    }
    failures += rootfs_stat_expect("/system/config-hard", 0, MR_INODE_REG);
    failures += rootfs_expect(
        "mknod rootfs directory",
        mr_fs_mknod("/system/devices", MR_INODE_DIR, MR_NULL), 0);
    failures += rootfs_stat_expect("/system/devices", 0, MR_INODE_DIR);

    /* Nested RootFS can be mounted and unmounted */
    failures += rootfs_expect("mkdir rootfs mount",
                              mr_fs_mkdir("/system/mnt"), 0);
    failures += rootfs_expect(
        "mount nested rootfs",
        mr_fs_mount(MR_NULL, "/system/mnt", "rootfs", 0, MR_NULL), 0);
    failures += rootfs_expect("mkdir nested rootfs",
                              mr_fs_mkdir("/system/mnt/child"), 0);
    failures += rootfs_stat_expect("/system/mnt/child", 0, MR_INODE_DIR);
    failures += rootfs_expect("umount nested rootfs",
                              mr_fs_umount("/system/mnt"), 0);

    /* Generic file creation is not a RootFS operation */
    failures += rootfs_expect(
        "reject rootfs file create",
        mr_fs_open(&file, "/created", MR_O_RDWR | MR_O_CREAT), -MR_ENOSYS);
    failures += rootfs_stat_expect("/created", -MR_ENOENT, MR_INODE_NONE);
    failures += rootfs_expect(
        "reject rootfs truncate open",
        mr_fs_open(&file, "/system/config", MR_O_WRONLY | MR_O_TRUNC),
        -MR_ENOSYS);

    /* Rename and remove structural nodes */
    failures += rootfs_expect("unlink rootfs symlink",
                              mr_fs_unlink_by_path("/system/config-link"), 0);
    failures += rootfs_expect("unlink rootfs hard link",
                              mr_fs_unlink_by_path("/system/config-hard"), 0);
    failures += rootfs_expect("unlink rootfs devices",
                              fs_test_unlink("/system/devices"), 0);
    failures += rootfs_expect("unlink rootfs mount point",
                              fs_test_unlink("/system/mnt"), 0);
    failures
        += rootfs_expect("rename rootfs node",
                          fs_test_rename("/system/config", "/system/settings"),
                          0);
    failures += rootfs_stat_expect("/system/config", -MR_ENOENT, MR_INODE_NONE);
    failures += rootfs_stat_expect("/system/settings", 0, MR_INODE_REG);
    failures += rootfs_expect("reject nonempty rootfs directory",
                               fs_test_unlink("/system"), -MR_ENOTEMPTY);
    failures += rootfs_expect("unlink rootfs settings",
                               fs_test_unlink("/system/settings"), 0);
    failures += rootfs_expect("unlink rootfs console",
                               fs_test_unlink("/system/console"), 0);
    failures += rootfs_expect("unlink rootfs services",
                               fs_test_unlink("/system/services"), 0);
    failures
        += rootfs_expect("unlink rootfs system", fs_test_unlink("/system"), 0);

    /* Unmount and verify all namespace allocations were released */
    failures += rootfs_expect("umount rootfs", mr_fs_umount("/"), 0);
    heap_after = mr_heap_information(MR_TRUE);
    failures += rootfs_expect("rootfs heap restored",
                              heap_after.used_bytes == heap_before.used_bytes,
                              MR_TRUE);
    return failures;
}

static void rootfs(void) {
    MR_TEST_EXPECT_EQ_INT(fs_rootfs_test(), 0);
}
MR_TEST_EXPORT(fs, rootfs, rootfs);
#endif /* defined(MR_USE_TEST) && defined(MR_USE_FS) */
