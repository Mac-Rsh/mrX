/**
 * @copyright (c) 2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2026-08-13        MacRsh        First version
 */

#include <mrX/test/mr_test.h>
#if defined(MR_USE_TEST) && defined(MR_USE_FS)
#include <mrX.h>
#include "fs_test.h"

static void fs_link_expect(volatile mr_uint32_t *__test_status,
                           const char *name, mr_int64_t actual,
                           mr_int64_t expected) {
    MR_IGNORE(name);

    MR_TEST_EXPECT_EQ_INT(actual, expected);
}

static void fs_link_test(volatile mr_uint32_t *__test_status) {
    mr_file_t source = MR_FILE_INIT(source);
    mr_file_t file = MR_FILE_INIT(file);
    mr_stat_t stat;
    char buffer[8];
    mr_err_t ret;

    /* Mount RamFS */
    ret = fs_test_mount_root("ramfs", 0, MR_NULL);
    fs_link_expect(__test_status, "link mount ramfs", ret, 0);
    if (ret != 0) {
        return;
    }

    /* Create a hard link sharing the same inode */
    ret = mr_fs_open(&source, "/source", MR_O_RDWR | MR_O_CREAT);
    fs_link_expect(__test_status, "open hard-link source", ret, 0);
    if (ret == 0) {
        fs_link_expect(__test_status, "write hard-link source",
                       mr_fs_write(&source, "data", 4), 4);
        fs_link_expect(__test_status, "create hard link",
                       mr_fs_link(&source, "/hard"), 0);
        fs_link_expect(__test_status, "stat linked source",
                       mr_fs_stat(&source, &stat), 0);
        fs_link_expect(__test_status, "source link count", stat.nlink, 2);
        fs_link_expect(__test_status, "unlink source", mr_fs_unlink(&source),
                       0);
        fs_link_expect(__test_status, "close source", mr_fs_close(&source), 0);
    }
    fs_link_expect(__test_status, "source removed",
                   fs_test_stat("/source", &stat), -MR_ENOENT);

    /* The remaining hard link preserves data and link count */
    ret = mr_fs_open(&file, "/hard", MR_O_RDONLY);
    fs_link_expect(__test_status, "open hard link", ret, 0);
    if (ret == 0) {
        fs_link_expect(__test_status, "read hard link",
                       mr_fs_read(&file, buffer, sizeof(buffer)), 4);
        if (mr_memcmp(buffer, "data", 4) != 0) {
            MR_TEST_EXPECT_EQ_INT(mr_memcmp(buffer, "data", 4), 0);
        }
        fs_link_expect(__test_status, "stat hard link",
                       mr_fs_stat(&file, &stat), 0);
        fs_link_expect(__test_status, "hard link count", stat.nlink, 1);
        fs_link_expect(__test_status, "close hard link", mr_fs_close(&file), 0);
    }

    /* Follow relative and absolute symbolic links */
    fs_link_expect(__test_status, "create relative symlink",
                   mr_fs_symlink("hard", "/relative"), 0);
    fs_link_expect(__test_status, "create absolute symlink",
                   mr_fs_symlink("/hard", "/absolute"), 0);
    fs_link_expect(__test_status, "stat relative symlink target",
                   fs_test_stat("/relative", &stat), 0);
    fs_link_expect(__test_status, "relative symlink target type", stat.type,
                   MR_INODE_REG);
    fs_link_expect(__test_status, "stat path follows symbolic link",
                   mr_fs_stat_by_path("/relative", &stat), 0);
    fs_link_expect(__test_status, "stat path symbolic target type", stat.type,
                   MR_INODE_REG);

    /* Open the link itself and read its stored target */
    ret = mr_fs_open(&file, "/absolute", MR_O_RDONLY | MR_O_NOFOLLOW);
    fs_link_expect(__test_status, "open symlink nofollow", ret, 0);
    if (ret == 0) {
        fs_link_expect(__test_status, "stat symlink itself",
                       mr_fs_stat(&file, &stat), 0);
        fs_link_expect(__test_status, "symlink inode type", stat.type,
                       MR_INODE_LNK);
        fs_link_expect(__test_status, "readlink absolute",
                       mr_fs_readlink(&file, buffer, sizeof(buffer)), 5);
        if (mr_memcmp(buffer, "/hard", 5) != 0) {
            MR_TEST_EXPECT_EQ_INT(mr_memcmp(buffer, "/hard", 5), 0);
        }
        fs_link_expect(__test_status, "close symlink", mr_fs_close(&file), 0);
    }

    /* Resolve links in intermediate path components */
    fs_link_expect(__test_status, "mkdir link directory", mr_fs_mkdir("/dir"),
                   0);
    ret = mr_fs_open(&file, "/dir/child", MR_O_RDWR | MR_O_CREAT);
    fs_link_expect(__test_status, "create linked child", ret, 0);
    if (ret == 0) {
        fs_link_expect(__test_status, "close linked child", mr_fs_close(&file),
                       0);
    }
    fs_link_expect(__test_status, "create directory symlink",
                   mr_fs_symlink("dir", "/dir-link"), 0);
    fs_link_expect(__test_status, "follow directory symlink",
                   fs_test_stat("/dir-link/child", &stat), 0);

    /* Path rename/unlink operate on the symbolic link itself */
    fs_link_expect(__test_status, "rename symbolic link",
                   mr_fs_rename_by_path("/relative", "/relative2"), 0);
    fs_link_expect(__test_status, "renamed link absent",
                   fs_test_stat("/relative", &stat), -MR_ENOENT);
    fs_link_expect(__test_status, "hard target remains",
                   fs_test_stat("/hard", &stat), 0);
    ret = mr_fs_open(&file, "/relative2", MR_O_RDONLY | MR_O_NOFOLLOW);
    fs_link_expect(__test_status, "open renamed link", ret, 0);
    if (ret == 0) {
        fs_link_expect(__test_status, "stat renamed link",
                       mr_fs_stat(&file, &stat), 0);
        fs_link_expect(__test_status, "renamed link type", stat.type,
                       MR_INODE_LNK);
        fs_link_expect(__test_status, "hard-link symbolic link",
                       mr_fs_link(&file, "/link-hard"), 0);
        fs_link_expect(__test_status, "stat linked symlink",
                       mr_fs_stat(&file, &stat), 0);
        fs_link_expect(__test_status, "symlink hard-link count", stat.nlink, 2);
        fs_link_expect(__test_status, "close renamed link", mr_fs_close(&file),
                       0);
    }
    fs_link_expect(__test_status, "unlink renamed link",
                   mr_fs_unlink_by_path("/relative2"), 0);
    fs_link_expect(__test_status, "hard target after unlink",
                   fs_test_stat("/hard", &stat), 0);
    fs_link_expect(__test_status, "remaining symlink hard-link",
                   mr_fs_statat_by_path(MR_NULL, "/link-hard", &stat,
                                        MR_AT_SYMLINK_NOFOLLOW),
                   0);
    fs_link_expect(__test_status, "remaining symlink type", stat.type,
                   MR_INODE_LNK);
    fs_link_expect(__test_status, "unlink symlink hard-link",
                   mr_fs_unlink_by_path("/link-hard"), 0);

    /* Detect symbolic-link loops */
    fs_link_expect(__test_status, "create loop one",
                   mr_fs_symlink("loop-two", "/loop-one"), 0);
    fs_link_expect(__test_status, "create loop two",
                   mr_fs_symlink("loop-one", "/loop-two"), 0);
    fs_link_expect(__test_status, "detect symlink loop",
                   mr_fs_open(&file, "/loop-one", MR_O_RDONLY), -MR_ELOOP);

    /* Follow a symbolic link across a nested mount */
    fs_link_expect(__test_status, "mkdir link mount-point",
                   mr_fs_mkdir("/mount"), 0);
    fs_link_expect(__test_status, "mount nested link fs",
                   mr_fs_mount(MR_NULL, "/mount", "ramfs", 0, MR_NULL), 0);
    ret = mr_fs_open(&file, "/mount/inside", MR_O_RDWR | MR_O_CREAT);
    fs_link_expect(__test_status, "create nested link target", ret, 0);
    if (ret == 0) {
        fs_link_expect(__test_status, "reject cross-fs hard link",
                       mr_fs_link(&file, "/cross-hard"), -MR_EXDEV);
        fs_link_expect(__test_status, "close nested link target",
                       mr_fs_close(&file), 0);
    }
    fs_link_expect(__test_status, "create cross-mount symlink",
                   mr_fs_symlink("/mount/inside", "/cross"), 0);
    fs_link_expect(__test_status, "follow cross-mount symlink",
                   fs_test_stat("/cross", &stat), 0);
    fs_link_expect(__test_status, "umount nested link fs",
                   mr_fs_umount("/mount"), 0);
    fs_link_expect(__test_status, "cross-mount symlink becomes dangling",
                   fs_test_stat("/cross", &stat), -MR_ENOENT);

    /* Unmount RamFS */
    fs_link_expect(__test_status, "link umount ramfs", mr_fs_umount("/"), 0);
    return;
}

MR_TEST(fs, link) {
    fs_link_test(__test_status);
}

#endif /* defined(MR_USE_TEST) && defined(MR_USE_FS) */
