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

static int dir_expect(const char *name, mr_int64_t actual,
                      mr_int64_t expected) {
    MR_IGNORE(name);

    MR_TEST_EXPECT_EQ_INT(actual, expected);
    return (actual == expected) ? 0 : 1;
}

static int dir_stat_expect(const char *path, mr_err_t expected) {
    mr_stat_t stat;

    return dir_expect(path, fs_test_stat(path, &stat), expected);
}

static int fs_dir_policy_test(void) {
    mr_file_t file = MR_FILE_INIT(file);
    mr_file_t dir = MR_FILE_INIT(dir);
    mr_dirent_t dirent;
    mr_bool_t found_x = MR_FALSE;
    mr_bool_t found_y = MR_FALSE;
    int failures = 0;
    mr_err_t ret;

    /* Mount RamFS */
    ret = mr_fs_mount(MR_NULL, "/", "ramfs", 0, MR_NULL);
    failures += dir_expect("mount ramfs", ret, 0);
    if (ret != 0) {
        return failures;
    }

    /* Empty directory unlink */
    failures += dir_expect("mkdir empty", mr_fs_mkdir("/empty"), 0);
    failures += dir_expect("unlink empty dir", fs_test_unlink("/empty"), 0);
    failures += dir_stat_expect("/empty", -MR_ENOENT);

    /* Non-empty directory unlink is rejected */
    failures += dir_expect("mkdir parent", mr_fs_mkdir("/parent"), 0);
    failures += dir_expect("mkdir parent/child",
                           mr_fs_mkdir("/parent/child"), 0);
    failures += dir_expect("unlink nonempty", fs_test_unlink("/parent"),
                           -MR_ENOTEMPTY);
    failures += dir_stat_expect("/parent", 0);
    failures += dir_stat_expect("/parent/child", 0);

    /* Unlink children then the parent */
    failures += dir_expect("unlink child",
                           fs_test_unlink("/parent/child"), 0);
    failures += dir_expect("unlink parent",
                           fs_test_unlink("/parent"), 0);
    failures += dir_stat_expect("/parent", -MR_ENOENT);

    /* mkdir missing parent / existing leaf */
    failures += dir_expect("mkdir missing parent",
                           mr_fs_mkdir("/nope/leaf"), -MR_ENOENT);
    failures += dir_expect("mkdir again", mr_fs_mkdir("/again"), 0);
    failures += dir_expect("mkdir exist", mr_fs_mkdir("/again"), -MR_EEXIST);

    /* Rename destination must not already exist */
    ret = mr_fs_open(&file, "/src", MR_O_RDWR | MR_O_CREAT);
    failures += dir_expect("create src", ret, 0);
    if (ret == 0) {
        failures += dir_expect("close src", mr_fs_close(&file), 0);
    }
    ret = mr_fs_open(&file, "/dst", MR_O_RDWR | MR_O_CREAT);
    failures += dir_expect("create dst", ret, 0);
    if (ret == 0) {
        failures += dir_expect("close dst", mr_fs_close(&file), 0);
    }
    failures += dir_expect("rename over exist",
                           fs_test_rename("/src", "/dst"), -MR_EEXIST);
    failures += dir_stat_expect("/src", 0);
    failures += dir_stat_expect("/dst", 0);

    /* Same-name rename is a no-op success */
    failures += dir_expect("rename same name",
                           fs_test_rename("/src", "/src"), 0);
    failures += dir_stat_expect("/src", 0);

    /* Cross-directory rename within the same FS */
    failures += dir_expect("mkdir move-a", mr_fs_mkdir("/move-a"), 0);
    failures += dir_expect("mkdir move-b", mr_fs_mkdir("/move-b"), 0);
    ret = mr_fs_open(&file, "/move-a/item", MR_O_RDWR | MR_O_CREAT);
    failures += dir_expect("create move item", ret, 0);
    if (ret == 0) {
        failures += dir_expect("write move item",
                               mr_fs_write(&file, "m", 1), 1);
        failures += dir_expect("close move item", mr_fs_close(&file), 0);
    }
    failures += dir_expect("rename cross dir",
                           fs_test_rename("/move-a/item", "/move-b/item"), 0);
    failures += dir_stat_expect("/move-a/item", -MR_ENOENT);
    failures += dir_stat_expect("/move-b/item", 0);

    /* Directory rename into its own descendant is rejected */
    failures += dir_expect("mkdir tree", mr_fs_mkdir("/tree"), 0);
    failures += dir_expect("mkdir tree/sub", mr_fs_mkdir("/tree/sub"), 0);
    failures += dir_expect(
        "rename into descendant",
        fs_test_rename("/tree", "/tree/sub/loop"), -MR_EINVAL);
    failures += dir_stat_expect("/tree", 0);
    failures += dir_stat_expect("/tree/sub", 0);

    /* Mount-point rename/unlink are busy */
    failures += dir_expect("mkdir mnt", mr_fs_mkdir("/mnt"), 0);
    failures += dir_expect("mount nested",
                           mr_fs_mount(MR_NULL, "/mnt", "ramfs", 0, MR_NULL),
                           0);
    failures += dir_expect("rename mountpoint",
                           fs_test_rename("/mnt", "/mnt2"), -MR_EBUSY);
    failures += dir_expect("unlink mountpoint",
                           fs_test_unlink("/mnt"), -MR_EBUSY);
    failures += dir_expect("umount nested", mr_fs_umount("/mnt"), 0);

    /* Root is a mount-point and cannot be unlinked */
    failures += dir_expect("unlink root", fs_test_unlink("/"), -MR_EBUSY);

    /* readdir enumerates siblings and ends with 0 */
    failures += dir_expect("mkdir readdir", mr_fs_mkdir("/readdir"), 0);
    ret = mr_fs_open(&file, "/readdir/x", MR_O_RDWR | MR_O_CREAT);
    failures += dir_expect("create readdir x", ret, 0);
    if (ret == 0) {
        failures += dir_expect("close readdir x", mr_fs_close(&file), 0);
    }
    ret = mr_fs_open(&file, "/readdir/y", MR_O_RDWR | MR_O_CREAT);
    failures += dir_expect("create readdir y", ret, 0);
    if (ret == 0) {
        failures += dir_expect("close readdir y", mr_fs_close(&file), 0);
    }
    ret = mr_fs_opendir(&dir, "/readdir");
    failures += dir_expect("opendir readdir", ret, 0);
    if (ret == 0) {
        while ((ret = mr_fs_readdir(&dir, &dirent)) > 0) {
            if (mr_strcmp(dirent.name, "x") == 0) {
                found_x = MR_TRUE;
            } else if (mr_strcmp(dirent.name, "y") == 0) {
                found_y = MR_TRUE;
            }
        }
        failures += dir_expect("readdir eof", ret, 0);
        failures += dir_expect("found x", found_x, MR_TRUE);
        failures += dir_expect("found y", found_y, MR_TRUE);
        failures += dir_expect("closedir readdir",
                               mr_fs_closedir(&dir), 0);
    }

    /* Directory rename within same parent */
    failures += dir_expect("rename dir",
                           fs_test_rename("/again", "/again2"), 0);
    failures += dir_stat_expect("/again", -MR_ENOENT);
    failures += dir_stat_expect("/again2", 0);

    /* Unlink by path for directories */
    failures += dir_expect("unlink by path empty dir",
                           mr_fs_unlink_by_path("/again2"), 0);
    failures += dir_stat_expect("/again2", -MR_ENOENT);

    /* Open-file still readable after umount of nested FS is covered elsewhere;
     * here ensure umount of root with no open handles succeeds and heap is
     * clean enough for a remount */
    failures += dir_expect("umount ramfs", mr_fs_umount("/"), 0);
    failures += dir_expect("remount ramfs",
                           mr_fs_mount(MR_NULL, "/", "ramfs", 0, MR_NULL), 0);
    failures += dir_expect("final umount", mr_fs_umount("/"), 0);
    return failures;
}

static void dir_policy(void) {
    MR_TEST_EXPECT_EQ_INT(fs_dir_policy_test(), 0);
}
MR_TEST_EXPORT(fs, dir_policy, dir_policy);
#endif /* defined(MR_USE_TEST) && defined(MR_USE_FS) */
