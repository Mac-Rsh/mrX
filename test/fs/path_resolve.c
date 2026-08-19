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

static void fs_path_resolve_test(volatile mr_uint32_t *__test_status) {
    mr_file_t file = MR_FILE_INIT(file);
    char long_path[MR_CFG_FS_PATH_LEN + 8];
    char join_target[MR_CFG_FS_PATH_LEN];
    char link_name[32];
    char target[32];
    mr_size_t target_len;
    mr_stat_t stat;
    mr_size_t i;
    mr_err_t ret;
#if !defined(MR_CFG_FS_SYMLINK_MAX)
#define MR_CFG_FS_SYMLINK_MAX (16U)
#endif

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
                mr_fs_stat_by_path("/a/file/x", &stat), -MR_ENOTDIR);
    path_expect(__test_status, "mkdir through file", mr_fs_mkdir("/a/file/x"),
                -MR_ENOTDIR);

    /* Path length limit: fs_lookup rejects len >= MR_CFG_FS_PATH_LEN */
    mr_memset(long_path, 'p', sizeof(long_path) - 1U);
    long_path[sizeof(long_path) - 1U] = '\0';
    long_path[0] = '/';
    path_expect(__test_status, "path too long",
                mr_fs_open(&file, long_path, MR_O_RDONLY), -MR_ENAMETOOLONG);
    path_expect(__test_status, "stat path too long",
                mr_fs_stat_by_path(long_path, &stat), -MR_ENAMETOOLONG);

    long_path[MR_CFG_FS_PATH_LEN] = '\0';
    path_expect(__test_status, "path max len",
                mr_fs_open(&file, long_path, MR_O_RDONLY), -MR_ENAMETOOLONG);

    /* Symlink depth: chain of length SYMLINK_MAX succeeds; +1 loops */
    path_expect(__test_status, "chdir root for links", mr_fs_chdir("/"), 0);
    ret = mr_fs_open(&file, "/chain0", MR_O_RDWR | MR_O_CREAT);
    path_expect(__test_status, "create chain0 target", ret, 0);
    if (ret == 0) {
        path_expect(__test_status, "close chain0", mr_fs_close(&file), 0);
    }

    for (i = 1; i <= (mr_size_t)MR_CFG_FS_SYMLINK_MAX; i++) {
        mr_snprintf(link_name, sizeof(link_name), "/chain%u", (unsigned)i);
        mr_snprintf(target, sizeof(target), "/chain%u", (unsigned)(i - 1U));
        path_expect(__test_status, link_name, mr_fs_symlink(target, link_name),
                    0);
    }

    /* Following SYMLINK_MAX links reaches the regular file */
    mr_snprintf(link_name, sizeof(link_name), "/chain%u",
                (unsigned)MR_CFG_FS_SYMLINK_MAX);
    path_expect(__test_status, "stat chain max", fs_test_stat(link_name, &stat),
                0);
    if (fs_test_stat(link_name, &stat) == 0) {
        path_expect(__test_status, "chain max type", stat.type, MR_INODE_REG);
    }

    /* One more link exceeds the limit */
    mr_snprintf(link_name, sizeof(link_name), "/chain%u",
                (unsigned)(MR_CFG_FS_SYMLINK_MAX + 1U));
    mr_snprintf(target, sizeof(target), "/chain%u",
                (unsigned)MR_CFG_FS_SYMLINK_MAX);
    path_expect(__test_status, "create over-depth link",
                mr_fs_symlink(target, link_name), 0);
    path_expect(__test_status, "open over-depth link",
                mr_fs_open(&file, link_name, MR_O_RDONLY), -MR_ELOOP);

    /* Joining a long symbolic-link target with a suffix overflows */
    target_len = (mr_size_t)MR_CFG_FS_PATH_LEN - 8U;
    mr_memset(join_target, 't', target_len);
    join_target[0] = '/';
    join_target[target_len] = '\0';
    path_expect(__test_status, "create long target link",
                mr_fs_symlink(join_target, "/long-link"), 0);
    path_expect(__test_status, "join overflow",
                mr_fs_open(&file, "/long-link/extra-component", MR_O_RDONLY),
                -MR_ENAMETOOLONG);

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

MR_TEST(fs, path_resolve) {
    fs_path_resolve_test(__test_status);
}

#endif /* defined(MR_USE_TEST) && defined(MR_USE_FS) */
