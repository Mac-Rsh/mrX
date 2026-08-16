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

static int fs_link_expect(const char *name, mr_int64_t actual,
                          mr_int64_t expected) {
    MR_IGNORE(name);

    MR_TEST_EXPECT_EQ_INT(actual, expected);
    return (actual == expected) ? 0 : 1;
}

static int fs_link_test(void) {
    mr_file_t source = MR_FILE_INIT(source);
    mr_file_t file = MR_FILE_INIT(file);
    mr_stat_t stat;
    char buffer[8];
    int failures = 0;
    mr_err_t ret;

    /* Mount RamFS */
    ret = mr_fs_mount(MR_NULL, "/", "ramfs", 0, MR_NULL);
    failures += fs_link_expect("link mount ramfs", ret, 0);
    if (ret != 0) {
        return failures;
    }

    /* Create a hard link sharing the same inode */
    ret = mr_fs_open(&source, "/source", MR_O_RDWR | MR_O_CREAT);
    failures += fs_link_expect("open hard-link source", ret, 0);
    if (ret == 0) {
        failures += fs_link_expect("write hard-link source",
                                   mr_fs_write(&source, "data", 4), 4);
        failures += fs_link_expect("create hard link",
                                   mr_fs_link(&source, "/hard"), 0);
        failures += fs_link_expect("stat linked source",
                                   mr_fs_stat(&source, &stat), 0);
        failures += fs_link_expect("source link count", stat.nlink, 2);
        failures += fs_link_expect("unlink source", mr_fs_unlink(&source), 0);
        failures += fs_link_expect("close source", mr_fs_close(&source), 0);
    }
    failures += fs_link_expect("source removed", fs_test_stat("/source", &stat),
                               -MR_ENOENT);

    /* The remaining hard link preserves data and link count */
    ret = mr_fs_open(&file, "/hard", MR_O_RDONLY);
    failures += fs_link_expect("open hard link", ret, 0);
    if (ret == 0) {
        failures += fs_link_expect(
            "read hard link", mr_fs_read(&file, buffer, sizeof(buffer)), 4);
        if (mr_memcmp(buffer, "data", 4) != 0) {
            MR_TEST_EXPECT_EQ_INT(mr_memcmp(buffer, "data", 4), 0);
            failures++;
        }
        failures
            += fs_link_expect("stat hard link", mr_fs_stat(&file, &stat), 0);
        failures += fs_link_expect("hard link count", stat.nlink, 1);
        failures += fs_link_expect("close hard link", mr_fs_close(&file), 0);
    }

    /* Follow relative and absolute symbolic links */
    failures += fs_link_expect("create relative symlink",
                               mr_fs_symlink("hard", "/relative"), 0);
    failures += fs_link_expect("create absolute symlink",
                               mr_fs_symlink("/hard", "/absolute"), 0);
    failures += fs_link_expect("stat relative symlink target",
                               fs_test_stat("/relative", &stat), 0);
    failures += fs_link_expect("relative symlink target type", stat.type,
                               MR_INODE_REG);
    failures += fs_link_expect("stat path follows symbolic link",
                               mr_fs_stat_by_path("/relative", &stat), 0);
    failures += fs_link_expect("stat path symbolic target type", stat.type,
                               MR_INODE_REG);

    /* Open the link itself and read its stored target */
    ret = mr_fs_open(&file, "/absolute", MR_O_RDONLY | MR_O_NOFOLLOW);
    failures += fs_link_expect("open symlink nofollow", ret, 0);
    if (ret == 0) {
        failures += fs_link_expect("stat symlink itself",
                                   mr_fs_stat(&file, &stat), 0);
        failures
            += fs_link_expect("symlink inode type", stat.type, MR_INODE_LNK);
        failures
            += fs_link_expect("readlink absolute",
                              mr_fs_readlink(&file, buffer, sizeof(buffer)), 5);
        if (mr_memcmp(buffer, "/hard", 5) != 0) {
            MR_TEST_EXPECT_EQ_INT(mr_memcmp(buffer, "/hard", 5), 0);
            failures++;
        }
        failures += fs_link_expect("close symlink", mr_fs_close(&file), 0);
    }

    /* Resolve links in intermediate path components */
    failures += fs_link_expect("mkdir link directory", mr_fs_mkdir("/dir"), 0);
    ret = mr_fs_open(&file, "/dir/child", MR_O_RDWR | MR_O_CREAT);
    failures += fs_link_expect("create linked child", ret, 0);
    if (ret == 0) {
        failures += fs_link_expect("close linked child", mr_fs_close(&file), 0);
    }
    failures += fs_link_expect("create directory symlink",
                               mr_fs_symlink("dir", "/dir-link"), 0);
    failures += fs_link_expect("follow directory symlink",
                               fs_test_stat("/dir-link/child", &stat), 0);

    /* Path rename/unlink operate on the symbolic link itself */
    failures += fs_link_expect(
        "rename symbolic link",
        mr_fs_rename_by_path("/relative", "/relative2"), 0);
    failures += fs_link_expect("renamed link absent",
                               fs_test_stat("/relative", &stat), -MR_ENOENT);
    failures += fs_link_expect("hard target remains",
                               fs_test_stat("/hard", &stat), 0);
    ret = mr_fs_open(&file, "/relative2", MR_O_RDONLY | MR_O_NOFOLLOW);
    failures += fs_link_expect("open renamed link", ret, 0);
    if (ret == 0) {
        failures += fs_link_expect("stat renamed link",
                                   mr_fs_stat(&file, &stat), 0);
        failures += fs_link_expect("renamed link type", stat.type,
                                   MR_INODE_LNK);
        failures += fs_link_expect(
            "hard-link symbolic link", mr_fs_link(&file, "/link-hard"), 0);
        failures += fs_link_expect("stat linked symlink",
                                   mr_fs_stat(&file, &stat), 0);
        failures += fs_link_expect("symlink hard-link count", stat.nlink, 2);
        failures += fs_link_expect("close renamed link", mr_fs_close(&file),
                                   0);
    }
    failures += fs_link_expect("unlink renamed link",
                               mr_fs_unlink_by_path("/relative2"), 0);
    failures += fs_link_expect("hard target after unlink",
                               fs_test_stat("/hard", &stat), 0);
    failures += fs_link_expect(
        "remaining symlink hard-link",
        mr_fs_statat(MR_NULL, "/link-hard", &stat, MR_AT_SYMLINK_NOFOLLOW),
        0);
    failures += fs_link_expect("remaining symlink type", stat.type,
                               MR_INODE_LNK);
    failures += fs_link_expect("unlink symlink hard-link",
                               mr_fs_unlink_by_path("/link-hard"), 0);

    /* Detect symbolic-link loops */
    failures += fs_link_expect("create loop one",
                               mr_fs_symlink("loop-two", "/loop-one"), 0);
    failures += fs_link_expect("create loop two",
                               mr_fs_symlink("loop-one", "/loop-two"), 0);
    failures += fs_link_expect("detect symlink loop",
                               mr_fs_open(&file, "/loop-one", MR_O_RDONLY),
                               -MR_ELOOP);

    /* Follow a symbolic link across a nested mount */
    failures
        += fs_link_expect("mkdir link mount-point", mr_fs_mkdir("/mount"), 0);
    failures += fs_link_expect(
        "mount nested link fs",
        mr_fs_mount(MR_NULL, "/mount", "ramfs", 0, MR_NULL), 0);
    ret = mr_fs_open(&file, "/mount/inside", MR_O_RDWR | MR_O_CREAT);
    failures += fs_link_expect("create nested link target", ret, 0);
    if (ret == 0) {
        failures += fs_link_expect("reject cross-fs hard link",
                                   mr_fs_link(&file, "/cross-hard"), -MR_EXDEV);
        failures += fs_link_expect("close nested link target",
                                   mr_fs_close(&file), 0);
    }
    failures += fs_link_expect("create cross-mount symlink",
                               mr_fs_symlink("/mount/inside", "/cross"), 0);
    failures += fs_link_expect("follow cross-mount symlink",
                               fs_test_stat("/cross", &stat), 0);
    failures
        += fs_link_expect("umount nested link fs", mr_fs_umount("/mount"), 0);
    failures += fs_link_expect("cross-mount symlink becomes dangling",
                               fs_test_stat("/cross", &stat), -MR_ENOENT);

    /* Unmount RamFS */
    failures += fs_link_expect("link umount ramfs", mr_fs_umount("/"), 0);
    return failures;
}

static void link(void) {
    MR_TEST_EXPECT_EQ_INT(fs_link_test(), 0);
}
MR_TEST_EXPORT(fs, link, link);
#endif /* defined(MR_USE_TEST) && defined(MR_USE_FS) */
