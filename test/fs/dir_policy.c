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
#include <mrX/test/fs/fs_test.h>

static void dir_expect(volatile mr_uint32_t *__test_status, const char *name,
                       mr_int64_t actual, mr_int64_t expected) {
    MR_IGNORE(name);

    MR_TEST_EXPECT_EQ_INT(actual, expected);
}

static void dir_stat_expect(volatile mr_uint32_t *__test_status,
                            const char *path, mr_err_t expected) {
    mr_stat_t stat;

    dir_expect(__test_status, path, fs_test_stat(path, &stat), expected);
}

MR_TEST(fs, dir_policy) {
    mr_file_t file = MR_FILE_INIT(file);
    mr_file_t dir = MR_FILE_INIT(dir);
    mr_dirent_t dirent;
    mr_bool_t found_x = MR_FALSE;
    mr_bool_t found_y = MR_FALSE;
    mr_err_t ret;

    /* Mount RamFS */
    ret = fs_test_mount_root("ramfs", 0, MR_NULL);
    dir_expect(__test_status, "mount ramfs", ret, 0);
    if (ret != 0) {
        return;
    }

    /* Empty directory removal */
    dir_expect(__test_status, "mkdir empty", mr_fs_mkdir("/empty"), 0);
    dir_expect(__test_status, "unlink rejects directory",
               mr_fs_unlink("/empty"), -MR_EISDIR);
    dir_expect(__test_status, "rmdir empty dir", mr_fs_rmdir("/empty"), 0);
    dir_stat_expect(__test_status, "/empty", -MR_ENOENT);

    /* Non-empty directory removal is rejected */
    dir_expect(__test_status, "mkdir parent", mr_fs_mkdir("/parent"), 0);
    dir_expect(__test_status, "mkdir parent/child",
               mr_fs_mkdir("/parent/child"), 0);
    dir_expect(__test_status, "rmdir nonempty", mr_fs_rmdir("/parent"),
               -MR_ENOTEMPTY);
    dir_stat_expect(__test_status, "/parent", 0);
    dir_stat_expect(__test_status, "/parent/child", 0);

    /* Remove children then the parent */
    dir_expect(__test_status, "rmdir child", mr_fs_rmdir("/parent/child"),
               0);
    dir_expect(__test_status, "rmdir parent", mr_fs_rmdir("/parent"), 0);
    dir_stat_expect(__test_status, "/parent", -MR_ENOENT);

    /* mkdir missing parent / existing leaf */
    dir_expect(__test_status, "mkdir missing parent", mr_fs_mkdir("/nope/leaf"),
               -MR_ENOENT);
    dir_expect(__test_status, "mkdir again", mr_fs_mkdir("/again"), 0);
    dir_expect(__test_status, "mkdir exist", mr_fs_mkdir("/again"), -MR_EEXIST);

    /* Rename destination must not already exist */
    ret = mr_fs_open(&file, "/src", MR_O_RDWR | MR_O_CREAT);
    dir_expect(__test_status, "create src", ret, 0);
    if (ret == 0) {
        dir_expect(__test_status, "close src", mr_fs_close(&file), 0);
    }
    ret = mr_fs_open(&file, "/dst", MR_O_RDWR | MR_O_CREAT);
    dir_expect(__test_status, "create dst", ret, 0);
    if (ret == 0) {
        dir_expect(__test_status, "close dst", mr_fs_close(&file), 0);
    }
    dir_expect(__test_status, "rename over exist",
               mr_fs_rename("/src", "/dst"), -MR_EEXIST);
    dir_stat_expect(__test_status, "/src", 0);
    dir_stat_expect(__test_status, "/dst", 0);

    /* Same-name rename is a no-op success */
    dir_expect(__test_status, "rename same name",
               mr_fs_rename("/src", "/src"), 0);
    dir_stat_expect(__test_status, "/src", 0);

    /* Cross-directory rename within the same FS */
    dir_expect(__test_status, "mkdir move-a", mr_fs_mkdir("/move-a"), 0);
    dir_expect(__test_status, "mkdir move-b", mr_fs_mkdir("/move-b"), 0);
    ret = mr_fs_open(&file, "/move-a/item", MR_O_RDWR | MR_O_CREAT);
    dir_expect(__test_status, "create move item", ret, 0);
    if (ret == 0) {
        dir_expect(__test_status, "write move item", mr_fs_write(&file, "m", 1),
                   1);
        dir_expect(__test_status, "close move item", mr_fs_close(&file), 0);
    }
    dir_expect(__test_status, "rename cross dir",
               mr_fs_rename("/move-a/item", "/move-b/item"), 0);
    dir_stat_expect(__test_status, "/move-a/item", -MR_ENOENT);
    dir_stat_expect(__test_status, "/move-b/item", 0);

    /* Directory rename into its own descendant is rejected */
    dir_expect(__test_status, "mkdir tree", mr_fs_mkdir("/tree"), 0);
    dir_expect(__test_status, "mkdir tree/sub", mr_fs_mkdir("/tree/sub"), 0);
    dir_expect(__test_status, "rename into descendant",
               mr_fs_rename("/tree", "/tree/sub/loop"), -MR_EINVAL);
    dir_stat_expect(__test_status, "/tree", 0);
    dir_stat_expect(__test_status, "/tree/sub", 0);

    /* Mount-point rename/unlink are busy */
    dir_expect(__test_status, "mkdir mnt", mr_fs_mkdir("/mnt"), 0);
    dir_expect(__test_status, "mount nested",
               mr_fs_mount(MR_NULL, "/mnt", "ramfs", 0, MR_NULL), 0);
    dir_expect(__test_status, "rename mountpoint",
               mr_fs_rename("/mnt", "/mnt2"), -MR_EBUSY);
    dir_expect(__test_status, "rmdir mountpoint", mr_fs_rmdir("/mnt"),
               -MR_EBUSY);
    dir_expect(__test_status, "umount nested", mr_fs_umount("/mnt"), 0);

    /* Root is a mount-point and cannot be unlinked */
    dir_expect(__test_status, "rmdir root", mr_fs_rmdir("/"), -MR_EBUSY);

    /* readdir enumerates siblings and ends with 0 */
    dir_expect(__test_status, "mkdir readdir", mr_fs_mkdir("/readdir"), 0);
    ret = mr_fs_open(&file, "/readdir/x", MR_O_RDWR | MR_O_CREAT);
    dir_expect(__test_status, "create readdir x", ret, 0);
    if (ret == 0) {
        dir_expect(__test_status, "close readdir x", mr_fs_close(&file), 0);
    }
    ret = mr_fs_open(&file, "/readdir/y", MR_O_RDWR | MR_O_CREAT);
    dir_expect(__test_status, "create readdir y", ret, 0);
    if (ret == 0) {
        dir_expect(__test_status, "close readdir y", mr_fs_close(&file), 0);
    }
    ret = mr_fs_open(&dir, "/readdir", MR_O_RDONLY | MR_O_DIRECTORY);
    dir_expect(__test_status, "opendir readdir", ret, 0);
    if (ret == 0) {
        while ((ret = mr_fs_readdir(&dir, &dirent)) > 0) {
            if (mr_strcmp(dirent.name, "x") == 0) {
                found_x = MR_TRUE;
            } else if (mr_strcmp(dirent.name, "y") == 0) {
                found_y = MR_TRUE;
            }
        }
        dir_expect(__test_status, "readdir eof", ret, 0);
        dir_expect(__test_status, "found x", found_x, MR_TRUE);
        dir_expect(__test_status, "found y", found_y, MR_TRUE);
        dir_expect(__test_status, "closedir readdir", mr_fs_close(&dir), 0);
    }

    /* Directory rename within same parent */
    dir_expect(__test_status, "rename dir", mr_fs_rename("/again", "/again2"),
               0);
    dir_stat_expect(__test_status, "/again", -MR_ENOENT);
    dir_stat_expect(__test_status, "/again2", 0);

    /* Remove the renamed directory */
    dir_expect(__test_status, "rmdir renamed directory",
               mr_fs_rmdir("/again2"), 0);
    dir_stat_expect(__test_status, "/again2", -MR_ENOENT);

    /* Open-file still readable after umount of nested FS is covered elsewhere;
     * here ensure umount of root with no open handles succeeds and heap is
     * clean enough for a remount */
    dir_expect(__test_status, "umount ramfs", mr_fs_umount("/"), 0);
    dir_expect(__test_status, "remount ramfs",
               fs_test_mount_root("ramfs", 0, MR_NULL), 0);
    dir_expect(__test_status, "final umount", mr_fs_umount("/"), 0);
    return;
}

#endif /* defined(MR_USE_TEST) && defined(MR_USE_FS) */
