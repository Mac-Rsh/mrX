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

static int treefs_expect(const char *name, mr_int64_t actual,
                         mr_int64_t expected) {
    MR_IGNORE(name);

    MR_TEST_EXPECT_EQ_INT(actual, expected);
    return (actual == expected) ? 0 : 1;
}

static int treefs_stat_expect(const char *path, mr_err_t expected,
                              mr_inode_type_t type) {
    mr_stat_t stat;
    mr_err_t ret;
    int failures;

    ret = fs_test_stat(path, &stat);
    failures = treefs_expect(path, ret, expected);
    if ((ret == 0) && (stat.type != type)) {
        MR_TEST_EXPECT_EQ_INT(stat.type, type);
        failures++;
    }
    return failures;
}

static int treefs_directory_expect(void) {
    mr_file_t directory = MR_FILE_INIT(directory);
    mr_bool_t found_services = MR_FALSE;
    mr_bool_t found_console = MR_FALSE;
    mr_bool_t found_config = MR_FALSE;
    mr_dirent_t dirent;
    int failures = 0;
    mr_err_t ret;

    /* Open the directory */
    ret = mr_fs_opendir(&directory, "/system");
    failures += treefs_expect("open treefs directory", ret, 0);
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
    failures += treefs_expect("finish treefs readdir", ret, 0);
    failures += treefs_expect("find treefs services", found_services, MR_TRUE);
    failures += treefs_expect("find treefs console", found_console, MR_TRUE);
    failures += treefs_expect("find treefs config", found_config, MR_TRUE);
    failures += treefs_expect("close treefs directory",
                               mr_fs_closedir(&directory), 0);
    return failures;
}

static int fs_treefs_test(void) {
    mr_file_t file = MR_FILE_INIT(file);
    mr_heap_info_t heap_before;
    mr_heap_info_t heap_after;
    char buffer[4];
    int metadata;
    int failures = 0;
    mr_err_t ret;

    /* Record heap state and mount TreeFS */
    heap_before = mr_heap_information(MR_TRUE);
    ret = mr_fs_mount(MR_NULL, "/", "treefs", 0, MR_NULL);
    failures += treefs_expect("mount treefs", ret, 0);
    if (ret != 0) {
        return failures;
    }

    /* Build a namespace tree */
    failures += treefs_expect("mkdir treefs system", mr_fs_mkdir("/system"), 0);
    failures += treefs_expect("mkdir treefs services",
                              mr_fs_mkdir("/system/services"), 0);
    failures += treefs_expect(
        "mknod treefs config",
        mr_fs_mknod("/system/config", MR_INODE_REG, &metadata), 0);
    failures += treefs_expect(
        "mknod treefs console",
        mr_fs_mknod("/system/console", MR_INODE_CHR, MR_NULL), 0);
    failures += treefs_stat_expect("/system", 0, MR_INODE_DIR);
    failures += treefs_stat_expect("/system/services", 0, MR_INODE_DIR);
    failures += treefs_stat_expect("/system/config", 0, MR_INODE_REG);
    failures += treefs_stat_expect("/system/console", 0, MR_INODE_CHR);
    failures += treefs_directory_expect();

    /* Nodes can be opened, but contain no file data */
    ret = mr_fs_open(&file, "/system/config", MR_O_RDWR);
    failures += treefs_expect("open treefs node", ret, 0);
    if (ret == 0) {
        failures += treefs_expect("treefs node private data",
                                  file.inode->priv == &metadata, MR_TRUE);
        failures += treefs_expect("reject treefs read",
                                   mr_fs_read(&file, buffer, sizeof(buffer)),
                                  -MR_ENOSYS);
        failures += treefs_expect("reject treefs write",
                                   mr_fs_write(&file, buffer, sizeof(buffer)),
                                  -MR_ENOSYS);
        failures += treefs_expect("reject treefs truncate",
                                   mr_fs_truncate(&file, 0), -MR_ENOSYS);
        failures += treefs_expect("close treefs node", mr_fs_close(&file), 0);
    }

    /* Built-in symbolic links and hard links work without TreeFS hooks */
    failures += treefs_expect(
        "create treefs symlink",
        mr_fs_symlink("/system/config", "/system/config-link"), 0);
    ret = mr_fs_open(&file, "/system/config-link",
                     MR_O_RDONLY | MR_O_NOFOLLOW);
    failures += treefs_expect("open treefs symlink", ret, 0);
    if (ret == 0) {
        char target[16];

        failures += treefs_expect(
            "readlink treefs",
            mr_fs_readlink(&file, target, sizeof(target)), 14);
        if (mr_memcmp(target, "/system/config", 14) != 0) {
            MR_TEST_EXPECT_EQ_INT(mr_memcmp(target, "/system/config", 14),
                                  0);
            failures++;
        }
        failures += treefs_expect("close treefs symlink",
                                  mr_fs_close(&file), 0);
    }
    ret = mr_fs_open(&file, "/system/config", MR_O_RDONLY);
    failures += treefs_expect("open treefs link source", ret, 0);
    if (ret == 0) {
        failures += treefs_expect(
            "hard-link treefs node",
            mr_fs_link(&file, "/system/config-hard"), 0);
        failures += treefs_expect("close treefs link source",
                                  mr_fs_close(&file), 0);
    }
    failures += treefs_stat_expect("/system/config-hard", 0, MR_INODE_REG);
    failures += treefs_expect(
        "mknod treefs directory",
        mr_fs_mknod("/system/devices", MR_INODE_DIR, MR_NULL), 0);
    failures += treefs_stat_expect("/system/devices", 0, MR_INODE_DIR);

    /* Nested TreeFS can be mounted and unmounted */
    failures += treefs_expect("mkdir treefs mount",
                              mr_fs_mkdir("/system/mnt"), 0);
    failures += treefs_expect(
        "mount nested treefs",
        mr_fs_mount(MR_NULL, "/system/mnt", "treefs", 0, MR_NULL), 0);
    failures += treefs_expect("mkdir nested treefs",
                              mr_fs_mkdir("/system/mnt/child"), 0);
    failures += treefs_stat_expect("/system/mnt/child", 0, MR_INODE_DIR);
    failures += treefs_expect("umount nested treefs",
                              mr_fs_umount("/system/mnt"), 0);

    /* Generic file creation is not a TreeFS operation */
    failures += treefs_expect(
        "reject treefs file create",
        mr_fs_open(&file, "/created", MR_O_RDWR | MR_O_CREAT), -MR_ENOSYS);
    failures += treefs_stat_expect("/created", -MR_ENOENT, MR_INODE_NONE);
    failures += treefs_expect(
        "reject treefs truncate open",
        mr_fs_open(&file, "/system/config", MR_O_WRONLY | MR_O_TRUNC),
        -MR_ENOSYS);

    /* Rename and remove structural nodes */
    failures += treefs_expect("unlink treefs symlink",
                              mr_fs_unlink_by_path("/system/config-link"), 0);
    failures += treefs_expect("unlink treefs hard link",
                              mr_fs_unlink_by_path("/system/config-hard"), 0);
    failures += treefs_expect("unlink treefs devices",
                              fs_test_unlink("/system/devices"), 0);
    failures += treefs_expect("unlink treefs mount point",
                              fs_test_unlink("/system/mnt"), 0);
    failures
        += treefs_expect("rename treefs node",
                          fs_test_rename("/system/config", "/system/settings"),
                          0);
    failures += treefs_stat_expect("/system/config", -MR_ENOENT, MR_INODE_NONE);
    failures += treefs_stat_expect("/system/settings", 0, MR_INODE_REG);
    failures += treefs_expect("reject nonempty treefs directory",
                               fs_test_unlink("/system"), -MR_ENOTEMPTY);
    failures += treefs_expect("unlink treefs settings",
                               fs_test_unlink("/system/settings"), 0);
    failures += treefs_expect("unlink treefs console",
                               fs_test_unlink("/system/console"), 0);
    failures += treefs_expect("unlink treefs services",
                               fs_test_unlink("/system/services"), 0);
    failures
        += treefs_expect("unlink treefs system", fs_test_unlink("/system"), 0);

    /* Unmount and verify all namespace allocations were released */
    failures += treefs_expect("umount treefs", mr_fs_umount("/"), 0);
    heap_after = mr_heap_information(MR_TRUE);
    failures += treefs_expect("treefs heap restored",
                              heap_after.used_bytes == heap_before.used_bytes,
                              MR_TRUE);
    return failures;
}

static void treefs(void) {
    MR_TEST_EXPECT_EQ_INT(fs_treefs_test(), 0);
}
MR_TEST_EXPORT(fs, treefs, treefs);
#endif /* defined(MR_USE_TEST) && defined(MR_USE_FS) */
