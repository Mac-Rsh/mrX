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

static void path_expect(volatile mr_uint32_t *__test_status, const char *name,
                        mr_int64_t actual, mr_int64_t expected) {
    MR_IGNORE(name);

    MR_TEST_EXPECT_EQ_INT(actual, expected);
}

static void path_stat_type(volatile mr_uint32_t *__test_status,
                           const char *path, mr_err_t expected,
                           mr_inode_type_t type) {
    mr_stat_t stat;
    mr_err_t ret;

    ret = fs_test_stat(path, &stat);
    path_expect(__test_status, path, ret, expected);
    if ((ret == 0) && (stat.type != type)) {
        MR_TEST_EXPECT_EQ_INT(stat.type, type);
    }
    return;
}

MR_TEST(fs, path_resolve) {
    mr_file_t file = MR_FILE_INIT(file);
    char long_path[MR_CFG_FS_PATH_LEN + 8];
    mr_stat_t stat;
    mr_err_t ret;

    /* Mount RamFS */
    ret = fs_test_mount_root("ramfs", 0, MR_NULL);
    path_expect(__test_status, "mount ramfs", ret, 0);
    if (ret != 0) {
        return;
    }

    /* Build a nested tree for . / .. resolution */
    path_expect(__test_status, "mkdir a", mr_fs_mkdir("/a"), 0);
    path_expect(__test_status, "mkdir a/b", mr_fs_mkdir("/a/b"), 0);
    path_expect(__test_status, "mkdir a/b/c", mr_fs_mkdir("/a/b/c"), 0);
    ret = mr_fs_open(&file, "/a/b/c/leaf", MR_O_RDWR | MR_O_CREAT);
    path_expect(__test_status, "create leaf", ret, 0);
    if (ret == 0) {
        path_expect(__test_status, "write leaf", mr_fs_write(&file, "L", 1), 1);
        path_expect(__test_status, "close leaf", mr_fs_close(&file), 0);
    }

    /* Multi-level . and .. */
    path_stat_type(__test_status, "/a/./b/../b/./c/leaf", 0, MR_INODE_REG);
    path_stat_type(__test_status, "/a/b/c/../../b/c/leaf", 0, MR_INODE_REG);
    path_stat_type(__test_status, "/a/b/c/../../../a/b/c", 0, MR_INODE_DIR);

    /* Root .. stays at root */
    path_expect(__test_status, "chdir root", mr_fs_chdir("/"), 0);
    path_stat_type(__test_status, "/..", 0, MR_INODE_DIR);
    path_stat_type(__test_status, "/../a", 0, MR_INODE_DIR);
    path_stat_type(__test_status, "/../../a/b", 0, MR_INODE_DIR);

    /* Relative .. from a nested cwd */
    path_expect(__test_status, "chdir a/b/c", mr_fs_chdir("/a/b/c"), 0);
    path_stat_type(__test_status, ".", 0, MR_INODE_DIR);
    path_stat_type(__test_status, "..", 0, MR_INODE_DIR);
    path_stat_type(__test_status, "../c/leaf", 0, MR_INODE_REG);
    path_stat_type(__test_status, "../../b/c/leaf", 0, MR_INODE_REG);
    path_stat_type(__test_status, "../../../a", 0, MR_INODE_DIR);

    /* Empty components via repeated slashes still resolve */
    path_stat_type(__test_status, "////a////b//c//leaf", 0, MR_INODE_REG);

    /* A regular file cannot be used as an intermediate component */
    ret = mr_fs_open(&file, "/a/file", MR_O_RDWR | MR_O_CREAT);
    path_expect(__test_status, "create intermediate file", ret, 0);
    if (ret == 0) {
        path_expect(__test_status, "close intermediate file",
                    mr_fs_close(&file), 0);
    }
    path_expect(__test_status, "open through file",
                mr_fs_open(&file, "/a/file/x", MR_O_RDONLY), -MR_ENOTDIR);
    path_expect(__test_status, "stat through file",
                fs_test_stat("/a/file/x", &stat), -MR_ENOTDIR);
    path_expect(__test_status, "mkdir through file", mr_fs_mkdir("/a/file/x"),
                -MR_ENOTDIR);

    /* Path length limit: fs_lookup rejects len >= MR_CFG_FS_PATH_LEN */
    mr_memset(long_path, 'p', sizeof(long_path) - 1U);
    long_path[sizeof(long_path) - 1U] = '\0';
    long_path[0] = '/';
    path_expect(__test_status, "path too long",
                mr_fs_open(&file, long_path, MR_O_RDONLY), -MR_ENAMETOOLONG);
    path_expect(__test_status, "stat path too long",
                fs_test_stat(long_path, &stat), -MR_ENAMETOOLONG);

    long_path[MR_CFG_FS_PATH_LEN] = '\0';
    path_expect(__test_status, "path max len",
                mr_fs_open(&file, long_path, MR_O_RDONLY), -MR_ENAMETOOLONG);

    /* Long dentry names use the heap path (beyond inline name length) */
    path_expect(__test_status, "mkdir long name", mr_fs_mkdir("/longnamedir"),
                0);
    ret = mr_fs_open(&file, "/longnamedir/longfilename",
                     MR_O_RDWR | MR_O_CREAT);
    path_expect(__test_status, "create long filename", ret, 0);
    if (ret == 0) {
        path_expect(__test_status, "write long filename",
                    mr_fs_write(&file, "ok", 2), 2);
        path_expect(__test_status, "close long filename", mr_fs_close(&file),
                    0);
    }
    path_stat_type(__test_status, "/longnamedir/longfilename", 0, MR_INODE_REG);

    /* Short names fit the inline buffer */
    ret = mr_fs_open(&file, "/ab", MR_O_RDWR | MR_O_CREAT);
    path_expect(__test_status, "create short name", ret, 0);
    if (ret == 0) {
        path_expect(__test_status, "close short name", mr_fs_close(&file), 0);
    }
    path_stat_type(__test_status, "/ab", 0, MR_INODE_REG);

    path_expect(__test_status, "umount ramfs", mr_fs_umount("/"), 0);
    return;
}

#endif /* defined(MR_USE_TEST) && defined(MR_USE_FS) */
