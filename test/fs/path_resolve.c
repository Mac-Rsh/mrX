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

static int path_expect(const char *name, mr_int64_t actual,
                       mr_int64_t expected) {
    MR_IGNORE(name);

    MR_TEST_EXPECT_EQ_INT(actual, expected);
    return (actual == expected) ? 0 : 1;
}

static int path_stat_type(const char *path, mr_err_t expected,
                          mr_inode_type_t type) {
    mr_stat_t stat;
    mr_err_t ret;
    int failures;

    ret = fs_test_stat(path, &stat);
    failures = path_expect(path, ret, expected);
    if ((ret == 0) && (stat.type != type)) {
        MR_TEST_EXPECT_EQ_INT(stat.type, type);
        failures++;
    }
    return failures;
}

static int fs_path_resolve_test(void) {
    mr_file_t file = MR_FILE_INIT(file);
    char long_path[MR_CFG_FS_PATH_LEN + 8];
    char join_target[MR_CFG_FS_PATH_LEN];
    char link_name[32];
    char target[32];
    mr_size_t target_len;
    mr_stat_t stat;
    mr_size_t i;
    int failures = 0;
    mr_err_t ret;
#if !defined(MR_CFG_FS_SYMLINK_MAX)
#define MR_CFG_FS_SYMLINK_MAX (16U)
#endif

    /* Mount RamFS */
    ret = mr_fs_mount(MR_NULL, "/", "ramfs", 0, MR_NULL);
    failures += path_expect("mount ramfs", ret, 0);
    if (ret != 0) {
        return failures;
    }

    /* Build a nested tree for . / .. resolution */
    failures += path_expect("mkdir a", mr_fs_mkdir("/a"), 0);
    failures += path_expect("mkdir a/b", mr_fs_mkdir("/a/b"), 0);
    failures += path_expect("mkdir a/b/c", mr_fs_mkdir("/a/b/c"), 0);
    ret = mr_fs_open(&file, "/a/b/c/leaf", MR_O_RDWR | MR_O_CREAT);
    failures += path_expect("create leaf", ret, 0);
    if (ret == 0) {
        failures += path_expect("write leaf", mr_fs_write(&file, "L", 1), 1);
        failures += path_expect("close leaf", mr_fs_close(&file), 0);
    }

    /* Multi-level . and .. */
    failures += path_stat_type("/a/./b/../b/./c/leaf", 0, MR_INODE_REG);
    failures += path_stat_type("/a/b/c/../../b/c/leaf", 0, MR_INODE_REG);
    failures += path_stat_type("/a/b/c/../../../a/b/c", 0, MR_INODE_DIR);

    /* Root .. stays at root */
    failures += path_expect("chdir root", mr_fs_chdir("/"), 0);
    failures += path_stat_type("/..", 0, MR_INODE_DIR);
    failures += path_stat_type("/../a", 0, MR_INODE_DIR);
    failures += path_stat_type("/../../a/b", 0, MR_INODE_DIR);

    /* Relative .. from a nested cwd */
    failures += path_expect("chdir a/b/c", mr_fs_chdir("/a/b/c"), 0);
    failures += path_stat_type(".", 0, MR_INODE_DIR);
    failures += path_stat_type("..", 0, MR_INODE_DIR);
    failures += path_stat_type("../c/leaf", 0, MR_INODE_REG);
    failures += path_stat_type("../../b/c/leaf", 0, MR_INODE_REG);
    failures += path_stat_type("../../../a", 0, MR_INODE_DIR);

    /* Empty components via repeated slashes still resolve */
    failures += path_stat_type("////a////b//c//leaf", 0, MR_INODE_REG);

    /* A regular file cannot be used as an intermediate component */
    ret = mr_fs_open(&file, "/a/file", MR_O_RDWR | MR_O_CREAT);
    failures += path_expect("create intermediate file", ret, 0);
    if (ret == 0) {
        failures += path_expect("close intermediate file",
                                mr_fs_close(&file), 0);
    }
    failures += path_expect(
        "open through file",
        mr_fs_open(&file, "/a/file/x", MR_O_RDONLY), -MR_ENOTDIR);
    failures += path_expect(
        "stat through file", mr_fs_stat_by_path("/a/file/x", &stat),
        -MR_ENOTDIR);
    failures += path_expect("mkdir through file", mr_fs_mkdir("/a/file/x"),
                            -MR_ENOTDIR);

    /* Path length limit: fs_lookup rejects len >= MR_CFG_FS_PATH_LEN */
    mr_memset(long_path, 'p', sizeof(long_path) - 1U);
    long_path[sizeof(long_path) - 1U] = '\0';
    long_path[0] = '/';
    failures += path_expect(
        "path too long", mr_fs_open(&file, long_path, MR_O_RDONLY),
        -MR_ENAMETOOLONG);
    failures += path_expect(
        "stat path too long", mr_fs_stat_by_path(long_path, &stat),
        -MR_ENAMETOOLONG);

    long_path[MR_CFG_FS_PATH_LEN] = '\0';
    failures += path_expect(
        "path max len", mr_fs_open(&file, long_path, MR_O_RDONLY),
        -MR_ENAMETOOLONG);

    /* Symlink depth: chain of length SYMLINK_MAX succeeds; +1 loops */
    failures += path_expect("chdir root for links", mr_fs_chdir("/"), 0);
    ret = mr_fs_open(&file, "/chain0", MR_O_RDWR | MR_O_CREAT);
    failures += path_expect("create chain0 target", ret, 0);
    if (ret == 0) {
        failures += path_expect("close chain0", mr_fs_close(&file), 0);
    }

    for (i = 1; i <= (mr_size_t)MR_CFG_FS_SYMLINK_MAX; i++) {
        mr_snprintf(link_name, sizeof(link_name), "/chain%u", (unsigned)i);
        mr_snprintf(target, sizeof(target), "/chain%u", (unsigned)(i - 1U));
        failures += path_expect(link_name, mr_fs_symlink(target, link_name),
                                0);
    }

    /* Following SYMLINK_MAX links reaches the regular file */
    mr_snprintf(link_name, sizeof(link_name), "/chain%u",
                (unsigned)MR_CFG_FS_SYMLINK_MAX);
    failures += path_expect("stat chain max",
                            fs_test_stat(link_name, &stat), 0);
    if (fs_test_stat(link_name, &stat) == 0) {
        failures += path_expect("chain max type", stat.type, MR_INODE_REG);
    }

    /* One more link exceeds the limit */
    mr_snprintf(link_name, sizeof(link_name), "/chain%u",
                (unsigned)(MR_CFG_FS_SYMLINK_MAX + 1U));
    mr_snprintf(target, sizeof(target), "/chain%u",
                (unsigned)MR_CFG_FS_SYMLINK_MAX);
    failures += path_expect("create over-depth link",
                            mr_fs_symlink(target, link_name), 0);
    failures += path_expect("open over-depth link",
                            mr_fs_open(&file, link_name, MR_O_RDONLY),
                            -MR_ELOOP);

    /* Joining a long symbolic-link target with a suffix overflows */
    target_len = (mr_size_t)MR_CFG_FS_PATH_LEN - 8U;
    mr_memset(join_target, 't', target_len);
    join_target[0] = '/';
    join_target[target_len] = '\0';
    failures += path_expect(
        "create long target link",
        mr_fs_symlink(join_target, "/long-link"), 0);
    failures += path_expect(
        "join overflow",
        mr_fs_open(&file, "/long-link/extra-component", MR_O_RDONLY),
        -MR_ENAMETOOLONG);

    /* Long dentry names use the heap path (beyond inline name length) */
    failures += path_expect(
        "mkdir long name",
        mr_fs_mkdir("/longnamedir"), 0);
    ret = mr_fs_open(&file, "/longnamedir/longfilename",
                     MR_O_RDWR | MR_O_CREAT);
    failures += path_expect("create long filename", ret, 0);
    if (ret == 0) {
        failures += path_expect("write long filename",
                                mr_fs_write(&file, "ok", 2), 2);
        failures += path_expect("close long filename",
                                mr_fs_close(&file), 0);
    }
    failures += path_stat_type("/longnamedir/longfilename", 0, MR_INODE_REG);

    /* Short names fit the inline buffer */
    ret = mr_fs_open(&file, "/ab", MR_O_RDWR | MR_O_CREAT);
    failures += path_expect("create short name", ret, 0);
    if (ret == 0) {
        failures += path_expect("close short name", mr_fs_close(&file), 0);
    }
    failures += path_stat_type("/ab", 0, MR_INODE_REG);

    failures += path_expect("umount ramfs", mr_fs_umount("/"), 0);
    return failures;
}

static void path_resolve(void) {
    MR_TEST_EXPECT_EQ_INT(fs_path_resolve_test(), 0);
}
MR_TEST_EXPORT(fs, path_resolve, path_resolve);
#endif /* defined(MR_USE_TEST) && defined(MR_USE_FS) */
