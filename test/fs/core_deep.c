/**
 * @copyright (c) 2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2026-08-30        MacRsh        First version
 */

#include <mrX/test/mr_test.h>
#if defined(MR_USE_TEST) && defined(MR_USE_FS) && defined(MR_USE_FS_RAMFS)
#include <mrX.h>
#include <mrX/test/fs/fs_test.h>

/* Overlong leaf length used to probe the missing leaf-length validation */
#define CORE_LONG_LEAF_LEN (300)
/* Create/unlink cycles for the heap balance check */
#define CORE_CHURN_CYCLES (6)

static void core_expect(volatile mr_uint32_t *__test_status, const char *name,
                        mr_int64_t actual, mr_int64_t expected) {
    MR_IGNORE(name);

    MR_TEST_EXPECT_EQ_INT(actual, expected);
}

static void core_stat_expect(volatile mr_uint32_t *__test_status,
                             const char *path, mr_err_t expected,
                             mr_inode_type_t type) {
    mr_stat_t stat;
    mr_err_t ret;

    ret = fs_test_stat(path, &stat);
    core_expect(__test_status, path, ret, expected);
    if ((ret == 0) && (stat.type != type)) {
        MR_TEST_EXPECT_EQ_INT(stat.type, type);
    }
    return;
}

MR_TEST(fs, core_trailing_slash) {
    mr_file_t file = MR_FILE_INIT(file);
    mr_err_t ret;

    /* Mount RamFS */
    ret = fs_test_mount_root("ramfs", 0, MR_NULL);
    core_expect(__test_status, "mount ramfs", ret, 0);
    if (ret != 0) {
        return;
    }

    /* Build a directory and a regular file */
    core_expect(__test_status, "mkdir d", mr_fs_mkdir("/d"), 0);
    ret = mr_fs_open(&file, "/d/f", MR_O_RDWR | MR_O_CREAT);
    core_expect(__test_status, "create f", ret, 0);
    if (ret == 0) {
        core_expect(__test_status, "write f", mr_fs_write(&file, "F", 1), 1);
        core_expect(__test_status, "close f", mr_fs_close(&file), 0);
    }

    /* A trailing slash is normalized for directories but requires files to
     * be directories */
    core_stat_expect(__test_status, "/d/", 0, MR_INODE_DIR);
    core_stat_expect(__test_status, "/d/f/", -MR_ENOTDIR, MR_INODE_REG);
    core_stat_expect(__test_status, "/d//f//", -MR_ENOTDIR, MR_INODE_REG);
    ret = mr_fs_open(&file, "/d/", MR_O_RDONLY | MR_O_DIRECTORY);
    core_expect(__test_status, "open dir trailing slash", ret, 0);
    if (ret == 0) {
        core_expect(__test_status, "close dir trailing slash",
                    mr_fs_close(&file), 0);
    }
    ret = mr_fs_open(&file, "/d/f/", MR_O_RDONLY);
    core_expect(__test_status, "open file trailing slash", ret, -MR_ENOTDIR);
    if (ret == 0) {
        core_expect(__test_status, "close file trailing slash",
                    mr_fs_close(&file), 0);
    }

    /* Creation rejects a trailing slash or a bare root leaf */
    core_expect(__test_status, "mkdir trailing slash", mr_fs_mkdir("/d2/"),
                -MR_EINVAL);
    core_expect(__test_status, "mkdir root", mr_fs_mkdir("/"), -MR_EINVAL);
    core_expect(__test_status, "mkdir double slash", mr_fs_mkdir("//"),
                -MR_EINVAL);
    core_expect(__test_status, "create trailing slash",
                mr_fs_open(&file, "/missing/", MR_O_RDWR | MR_O_CREAT),
                -MR_EINVAL);

    /* Clean up */
    core_expect(__test_status, "unlink f", mr_fs_unlink("/d/f"), 0);
    core_expect(__test_status, "rmdir d", mr_fs_rmdir("/d"), 0);
    core_expect(__test_status, "umount ramfs", mr_fs_umount("/"), 0);
    return;
}

MR_TEST(fs, core_name_length_bounds) {
    static const mr_size_t core_name_lens[3] = {31, 32, 33};
    mr_file_t file = MR_FILE_INIT(file);
    char path[MR_CFG_FS_PATH_LEN + 16];
    mr_size_t i;
    mr_err_t ret;

    /* Mount RamFS */
    ret = fs_test_mount_root("ramfs", 0, MR_NULL);
    core_expect(__test_status, "mount ramfs", ret, 0);
    if (ret != 0) {
        return;
    }

    /* Names around the inline boundary (32) use both storage paths */
    for (i = 0; i < 3U; i++) {
        path[0] = '/';
        mr_memset(&path[1], 'n', core_name_lens[i]);
        path[1 + core_name_lens[i]] = '\0';
        ret = mr_fs_open(&file, path, MR_O_RDWR | MR_O_CREAT);
        core_expect(__test_status, "create boundary name", ret, 0);
        if (ret == 0) {
            core_expect(__test_status, "close boundary name",
                        mr_fs_close(&file), 0);
        }
        core_stat_expect(__test_status, path, 0, MR_INODE_REG);
        core_expect(__test_status, "unlink boundary name", mr_fs_unlink(path),
                    0);
    }

    /* A 255-character component keeps the path below the limit */
    mr_memset(path, 'p', 255U);
    path[255U] = '\0';
    path[0] = '/';
    ret = mr_fs_open(&file, path, MR_O_RDWR | MR_O_CREAT);
    core_expect(__test_status, "create 255 name", ret, 0);
    if (ret == 0) {
        core_expect(__test_status, "close 255 name", mr_fs_close(&file), 0);
    }
    core_stat_expect(__test_status, path, 0, MR_INODE_REG);
    core_expect(__test_status, "unlink 255 name", mr_fs_unlink(path), 0);

    /* A 258-character component leaves a 259-character path */
    mr_memset(path, 'q', 258U);
    path[258U] = '\0';
    path[0] = '/';
    ret = mr_fs_open(&file, path, MR_O_RDWR | MR_O_CREAT);
    core_expect(__test_status, "create 258 name", ret, 0);
    if (ret == 0) {
        core_expect(__test_status, "close 258 name", mr_fs_close(&file), 0);
    }
    core_stat_expect(__test_status, path, 0, MR_INODE_REG);
    core_expect(__test_status, "unlink 258 name", mr_fs_unlink(path), 0);

    /* A 259-character component reaches exactly the path limit */
    mr_memset(path, 'r', 259U);
    path[259U] = '\0';
    path[0] = '/';
    ret = mr_fs_open(&file, path, MR_O_RDWR | MR_O_CREAT);
    core_expect(__test_status, "create 259 name", ret, 0);
    if (ret == 0) {
        core_expect(__test_status, "close 259 name", mr_fs_close(&file), 0);
    }
    core_stat_expect(__test_status, path, 0, MR_INODE_REG);
    core_expect(__test_status, "unlink 259 name", mr_fs_unlink(path), 0);

    /* A 260-character component makes the path exceed the limit */
    mr_memset(path, 's', 260U);
    path[260U] = '\0';
    path[0] = '/';
    core_expect(__test_status, "create 260 name",
                mr_fs_open(&file, path, MR_O_RDWR | MR_O_CREAT),
                -MR_ENAMETOOLONG);
    core_stat_expect(__test_status, path, -MR_ENAMETOOLONG, MR_INODE_REG);

    core_expect(__test_status, "umount ramfs", mr_fs_umount("/"), 0);
    return;
}

MR_TEST(fs, core_leaf_length_unchecked) {
    mr_file_t file = MR_FILE_INIT(file);
    char path[MR_CFG_FS_PATH_LEN + 64];
    mr_err_t ret;

    /* Mount RamFS */
    ret = fs_test_mount_root("ramfs", 0, MR_NULL);
    core_expect(__test_status, "mount ramfs", ret, 0);
    if (ret != 0) {
        return;
    }
    core_expect(__test_status, "mkdir keep", mr_fs_mkdir("/keep"), 0);

    /* Build "/keep/<300 characters>" */
    mr_memset(path, 'n', CORE_LONG_LEAF_LEN);
    path[CORE_LONG_LEAF_LEN] = '\0';
    mr_memcpy(path, "/keep/", 6U);

    /* Creation entry points must enforce the full path limit */
    core_expect(__test_status, "mkdir overlong leaf", mr_fs_mkdir(path),
                -MR_ENAMETOOLONG);
    core_stat_expect(__test_status, path, -MR_ENAMETOOLONG, MR_INODE_DIR);

    /* O_CREAT is guarded by the full-path length check in fs_lookup */
    ret = mr_fs_open(&file, path, MR_O_RDWR | MR_O_CREAT);
    core_expect(__test_status, "create overlong leaf", ret, -MR_ENAMETOOLONG);
    if (ret == 0) {
        core_expect(__test_status, "close overlong leaf", mr_fs_close(&file),
                    0);
    }

    /* rename validates only the destination parent, not the leaf */
    ret = mr_fs_open(&file, "/keep/movable", MR_O_RDWR | MR_O_CREAT);
    core_expect(__test_status, "create movable", ret, 0);
    if (ret == 0) {
        core_expect(__test_status, "close movable", mr_fs_close(&file), 0);
    }

    /* Rename must enforce the same full path limit */
    core_expect(__test_status, "rename overlong leaf",
                mr_fs_rename("/keep/movable", path), -MR_ENAMETOOLONG);

    /* Tolerant cleanup: the bug leaves unremovable nodes behind */
    ret = mr_fs_unlink("/keep/movable");
    if (ret != 0) {
        core_expect(__test_status, "movable moved away", ret, -MR_ENOENT);
    }
    ret = mr_fs_rmdir(path);
    if (ret != 0) {
        core_expect(__test_status, "overlong dir unremovable", ret,
                    -MR_ENAMETOOLONG);
    }
    core_expect(__test_status, "rmdir keep", mr_fs_rmdir("/keep"), 0);
    core_expect(__test_status, "umount ramfs", mr_fs_umount("/"), 0);
    return;
}

MR_TEST(fs, core_dir_lseek) {
    static const char *const core_dir_names[3] = {"da", "db", "dc"};
    mr_file_t file = MR_FILE_INIT(file);
    mr_file_t dir = MR_FILE_INIT(dir);
    mr_dirent_t dirent;
    mr_size_t i;
    char path[16];
    mr_err_t ret;

    /* Mount RamFS */
    ret = fs_test_mount_root("ramfs", 0, MR_NULL);
    core_expect(__test_status, "mount ramfs", ret, 0);
    if (ret != 0) {
        return;
    }

    /* Three children in creation order */
    core_expect(__test_status, "mkdir dh", mr_fs_mkdir("/dh"), 0);
    for (i = 0; i < 3U; i++) {
        mr_snprintf(path, sizeof(path), "/dh/%s", core_dir_names[i]);
        ret = mr_fs_open(&file, path, MR_O_WRONLY | MR_O_CREAT);
        core_expect(__test_status, "create dir child", ret, 0);
        if (ret == 0) {
            core_expect(__test_status, "close dir child", mr_fs_close(&file),
                        0);
        }
    }

    /* Sequential readdir walks the children in creation order */
    ret = mr_fs_open(&dir, "/dh", MR_O_RDONLY | MR_O_DIRECTORY);
    core_expect(__test_status, "opendir dh", ret, 0);
    if (ret != 0) {
        goto _exit;
    }
    ret = mr_fs_readdir(&dir, &dirent);
    core_expect(__test_status, "readdir first", ret, 1);
    if (ret == 1) {
        core_expect(__test_status, "first name", mr_strcmp(dirent.name, "da"),
                    0);
        core_expect(__test_status, "first type", dirent.type, MR_INODE_REG);
    }

    /* Seek back to zero replays the first entry */
    core_expect(__test_status, "seek dir zero",
                mr_fs_lseek(&dir, 0, MR_SEEK_SET), 0);
    ret = mr_fs_readdir(&dir, &dirent);
    core_expect(__test_status, "replay first", ret, 1);
    if (ret == 1) {
        core_expect(__test_status, "replay name", mr_strcmp(dirent.name, "da"),
                    0);
    }

    /* Seek forward skips entries */
    core_expect(__test_status, "seek dir skip",
                mr_fs_lseek(&dir, 2, MR_SEEK_SET), 2);
    ret = mr_fs_readdir(&dir, &dirent);
    core_expect(__test_status, "readdir skipped", ret, 1);
    if (ret == 1) {
        core_expect(__test_status, "skipped name", mr_strcmp(dirent.name, "dc"),
                    0);
    }

    /* A position beyond the child count reports end-of-directory */
    core_expect(__test_status, "seek dir past end",
                mr_fs_lseek(&dir, 100, MR_SEEK_SET), 100);
    core_expect(__test_status, "readdir past end", mr_fs_readdir(&dir, &dirent),
                0);

    /* Seek back within range is valid and still reports no entries there */
    core_expect(__test_status, "seek dir back",
                mr_fs_lseek(&dir, -1, MR_SEEK_CUR), 99);
    core_expect(__test_status, "readdir after back seek",
                mr_fs_readdir(&dir, &dirent), 0);

    /* A negative resulting position is rejected and keeps the position */
    core_expect(__test_status, "seek dir negative set",
                mr_fs_lseek(&dir, -5, MR_SEEK_SET), -MR_EINVAL);
    core_expect(__test_status, "readdir after rejected seek",
                mr_fs_readdir(&dir, &dirent), 0);
    core_expect(__test_status, "seek dir negative set",
                mr_fs_lseek(&dir, -5, MR_SEEK_SET), -MR_EINVAL);

    core_expect(__test_status, "closedir dh", mr_fs_close(&dir), 0);
_exit:
    for (i = 0; i < 3U; i++) {
        mr_snprintf(path, sizeof(path), "/dh/%s", core_dir_names[i]);
        core_expect(__test_status, "unlink dir child", mr_fs_unlink(path), 0);
    }
    core_expect(__test_status, "rmdir dh", mr_fs_rmdir("/dh"), 0);
    core_expect(__test_status, "umount ramfs", mr_fs_umount("/"), 0);
    return;
}

MR_TEST(fs, core_dir_reuse_and_cwd) {
    mr_file_t file = MR_FILE_INIT(file);
    mr_file_t dir = MR_FILE_INIT(dir);
    mr_dirent_t dirent;
    char cwd[8];
    mr_ssize_t cwd_ret;
    mr_err_t ret;

    /* Mount RamFS */
    ret = fs_test_mount_root("ramfs", 0, MR_NULL);
    core_expect(__test_status, "mount ramfs", ret, 0);
    if (ret != 0) {
        return;
    }

    /* Open a directory handle and drain it */
    core_expect(__test_status, "mkdir dh2", mr_fs_mkdir("/dh2"), 0);
    ret = mr_fs_open(&file, "/dh2/x", MR_O_WRONLY | MR_O_CREAT);
    core_expect(__test_status, "create x", ret, 0);
    if (ret == 0) {
        core_expect(__test_status, "close x", mr_fs_close(&file), 0);
    }
    ret = mr_fs_open(&dir, "/dh2", MR_O_RDONLY | MR_O_DIRECTORY);
    core_expect(__test_status, "opendir dh2", ret, 0);
    if (ret != 0) {
        goto _exit;
    }
    ret = mr_fs_readdir(&dir, &dirent);
    core_expect(__test_status, "readdir x", ret, 1);
    if (ret == 1) {
        core_expect(__test_status, "x name", mr_strcmp(dirent.name, "x"), 0);
    }
    core_expect(__test_status, "readdir eof", mr_fs_readdir(&dir, &dirent), 0);

    /* Replacing children behind the handle keeps the old position */
    core_expect(__test_status, "unlink x", mr_fs_unlink("/dh2/x"), 0);
    ret = mr_fs_open(&file, "/dh2/y", MR_O_WRONLY | MR_O_CREAT);
    core_expect(__test_status, "create y", ret, 0);
    if (ret == 0) {
        core_expect(__test_status, "close y", mr_fs_close(&file), 0);
    }
    core_expect(__test_status, "readdir old position",
                mr_fs_readdir(&dir, &dirent), 0);
    core_expect(__test_status, "seek dir restart",
                mr_fs_lseek(&dir, 0, MR_SEEK_SET), 0);
    ret = mr_fs_readdir(&dir, &dirent);
    core_expect(__test_status, "readdir y", ret, 1);
    if (ret == 1) {
        core_expect(__test_status, "y name", mr_strcmp(dirent.name, "y"), 0);
    }

    /* Removing the open directory leaves the handle empty but valid */
    core_expect(__test_status, "unlink y", mr_fs_unlink("/dh2/y"), 0);
    core_expect(__test_status, "rmdir open dir", mr_fs_rmdir("/dh2"), 0);
    core_stat_expect(__test_status, "/dh2", -MR_ENOENT, MR_INODE_DIR);
    core_expect(__test_status, "readdir removed dir",
                mr_fs_readdir(&dir, &dirent), 0);
    core_expect(__test_status, "closedir removed", mr_fs_close(&dir), 0);

    /* The current working directory can be removed under the process */
    core_expect(__test_status, "mkdir doomed", mr_fs_mkdir("/doomed"), 0);
    core_expect(__test_status, "chdir doomed", mr_fs_chdir("/doomed"), 0);
    core_expect(__test_status, "rmdir cwd", mr_fs_rmdir("/doomed"), 0);
    cwd_ret = mr_fs_getcwd(cwd, sizeof(cwd));
    core_expect(__test_status, "getcwd detached", cwd_ret, -MR_ENOENT);
    core_expect(__test_status, "relative create detached",
                mr_fs_open(&file, "rel", MR_O_RDWR | MR_O_CREAT), -MR_ENOENT);
    core_expect(__test_status, "chdir root recover", mr_fs_chdir("/"), 0);
    cwd_ret = mr_fs_getcwd(cwd, sizeof(cwd));
    core_expect(__test_status, "getcwd root", cwd_ret, 1);
    if ((cwd_ret == 1) && (cwd[0] != '/')) {
        MR_TEST_EXPECT_EQ_INT(cwd[0], '/');
    }

_exit:
    if (dir.inode) {
        core_expect(__test_status, "close dir cleanup", mr_fs_close(&dir), 0);
    }
    core_expect(__test_status, "umount ramfs", mr_fs_umount("/"), 0);
    return;
}

MR_TEST(fs, core_mount_dots) {
    mr_file_t file = MR_FILE_INIT(file);
    mr_err_t ret;

    /* Mount RamFS */
    ret = fs_test_mount_root("ramfs", 0, MR_NULL);
    core_expect(__test_status, "mount ramfs", ret, 0);
    if (ret != 0) {
        return;
    }

    /* Mount through a normalized absolute path */
    core_expect(__test_status, "mkdir mnt0", mr_fs_mkdir("/mnt0"), 0);
    core_expect(__test_status, "mount via dots",
                mr_fs_mount(MR_NULL, "/./mnt0/../mnt0", "ramfs", 0, MR_NULL),
                0);
    core_expect(__test_status, "repeat mount",
                mr_fs_mount(MR_NULL, "/mnt0", "ramfs", 0, MR_NULL), -MR_EBUSY);

    /* Populate the first mount and stack a second one inside it */
    ret = mr_fs_open(&file, "/mnt0/inside", MR_O_RDWR | MR_O_CREAT);
    core_expect(__test_status, "create inside", ret, 0);
    if (ret == 0) {
        core_expect(__test_status, "close inside", mr_fs_close(&file), 0);
    }
    core_expect(__test_status, "mkdir inner", mr_fs_mkdir("/mnt0/inner"), 0);
    core_expect(__test_status, "mount inner dots",
                mr_fs_mount(MR_NULL, "/mnt0/./inner", "ramfs", 0, MR_NULL), 0);
    ret = mr_fs_open(&file, "/mnt0/inner/deep", MR_O_RDWR | MR_O_CREAT);
    core_expect(__test_status, "create deep", ret, 0);
    if (ret == 0) {
        core_expect(__test_status, "close deep", mr_fs_close(&file), 0);
    }

    /* Unmount the child through a trailing slash path */
    core_expect(__test_status, "umount inner trailing slash",
                mr_fs_umount("/mnt0/inner/"), 0);
    core_stat_expect(__test_status, "/mnt0/inside", 0, MR_INODE_REG);

    /* Unmount the parent through the mount-point path */
    core_expect(__test_status, "umount parent", mr_fs_umount("/mnt0"), 0);
    core_stat_expect(__test_status, "/mnt0/inside", -MR_ENOENT, MR_INODE_REG);

    /* Clean up */
    core_expect(__test_status, "rmdir mnt0", mr_fs_rmdir("/mnt0"), 0);
    core_expect(__test_status, "umount ramfs", mr_fs_umount("/"), 0);
    return;
}

MR_TEST(fs, core_mount_chain_teardown) {
    mr_file_t file = MR_FILE_INIT(file);
    mr_stat_t stat;
    char buffer[8];
    mr_ssize_t io_ret;
    mr_err_t ret;

    /* Mount RamFS */
    ret = fs_test_mount_root("ramfs", 0, MR_NULL);
    core_expect(__test_status, "mount ramfs", ret, 0);
    if (ret != 0) {
        return;
    }

    /* Build a two-level mount chain */
    core_expect(__test_status, "mkdir m1", mr_fs_mkdir("/m1"), 0);
    core_expect(__test_status, "mount m1",
                mr_fs_mount(MR_NULL, "/m1", "ramfs", 0, MR_NULL), 0);
    core_expect(__test_status, "mkdir m2", mr_fs_mkdir("/m1/m2"), 0);
    core_expect(__test_status, "mount m2",
                mr_fs_mount(MR_NULL, "/m1/m2", "ramfs", 0, MR_NULL), 0);

    /* Keep a file open inside the deepest mount */
    ret = mr_fs_open(&file, "/m1/m2/f", MR_O_RDWR | MR_O_CREAT);
    core_expect(__test_status, "create f", ret, 0);
    if (ret != 0) {
        goto _exit;
    }
    core_expect(__test_status, "write f", mr_fs_write(&file, "V", 1), 1);

    /* Graceful unmount rejects open files and parent mounts with children */
    core_expect(__test_status, "umount m2 with open file",
                mr_fs_umount("/m1/m2"), -MR_EBUSY);
    core_expect(__test_status, "umount m1 with child mount",
                mr_fs_umount("/m1"), -MR_EBUSY);

    /* Rejected unmounts leave the handle and mount chain usable */
    ret = mr_fs_fstat(&file, &stat);
    core_expect(__test_status, "stat file after busy umount", ret, 0);
    if (ret == 0) {
        core_expect(__test_status, "file size after busy umount", stat.size, 1);
    }
    core_expect(__test_status, "seek file after busy umount",
                mr_fs_lseek(&file, 0, MR_SEEK_SET), 0);
    io_ret = mr_fs_read(&file, buffer, sizeof(buffer));
    core_expect(__test_status, "read file after busy umount", io_ret, 1);
    if ((io_ret == 1) && (buffer[0] != 'V')) {
        MR_TEST_EXPECT_EQ_INT(buffer[0], 'V');
    }
    core_expect(__test_status, "close open file", mr_fs_close(&file), 0);

    /* Closed mounts tear down bottom-up */
    core_expect(__test_status, "umount m2 after close", mr_fs_umount("/m1/m2"),
                0);
    core_expect(__test_status, "umount m1 after child", mr_fs_umount("/m1"), 0);

    /* The covered directory is visible and empty again */
    core_stat_expect(__test_status, "/m1", 0, MR_INODE_DIR);
    core_expect(__test_status, "rmdir m1", mr_fs_rmdir("/m1"), 0);

_exit:
    if (file.inode) {
        core_expect(__test_status, "close file cleanup", mr_fs_close(&file), 0);
    }

    /* Best-effort teardown so a failure never blocks the root unmount */
    mr_fs_umount("/m1/m2");
    mr_fs_umount("/m1");
    core_expect(__test_status, "umount ramfs", mr_fs_umount("/"), 0);
    return;
}

MR_TEST(fs, core_append_interleave) {
    mr_file_t first = MR_FILE_INIT(first);
    mr_file_t second = MR_FILE_INIT(second);
    mr_file_t reader = MR_FILE_INIT(reader);
    char buffer[8];
    mr_ssize_t io_ret;
    mr_err_t ret;

    /* Mount RamFS */
    ret = fs_test_mount_root("ramfs", 0, MR_NULL);
    core_expect(__test_status, "mount ramfs", ret, 0);
    if (ret != 0) {
        return;
    }

    /* Two write-only append handles share the end-of-file selector */
    ret = mr_fs_open(&first, "/app", MR_O_WRONLY | MR_O_APPEND | MR_O_CREAT);
    core_expect(__test_status, "open first append", ret, 0);
    if (ret != 0) {
        goto _exit;
    }
    core_expect(__test_status, "append read rejected",
                mr_fs_read(&first, buffer, sizeof(buffer)), -MR_EBADF);
    core_expect(__test_status, "append one", mr_fs_write(&first, "1", 1), 1);
    ret = mr_fs_open(&second, "/app", MR_O_WRONLY | MR_O_APPEND);
    core_expect(__test_status, "open second append", ret, 0);
    if (ret != 0) {
        goto _exit;
    }
    core_expect(__test_status, "append two", mr_fs_write(&second, "2", 1), 1);

    /* Seeking an append handle does not move the write point */
    core_expect(__test_status, "seek first append",
                mr_fs_lseek(&first, 0, MR_SEEK_SET), 0);
    core_expect(__test_status, "append three", mr_fs_write(&first, "3", 1), 1);

    /* An external truncate pulls the append point back */
    ret = mr_fs_open(&reader, "/app", MR_O_WRONLY);
    core_expect(__test_status, "open truncate handle", ret, 0);
    if (ret == 0) {
        core_expect(__test_status, "truncate to one",
                    mr_fs_ftruncate(&reader, 1), 0);
        core_expect(__test_status, "close truncate handle",
                    mr_fs_close(&reader), 0);
    }
    core_expect(__test_status, "seek before reappend",
                mr_fs_lseek(&first, 0, MR_SEEK_SET), 0);
    core_expect(__test_status, "append four", mr_fs_write(&first, "4", 1), 1);
    core_expect(__test_status, "append position",
                mr_fs_lseek(&first, 0, MR_SEEK_CUR), 2);

    /* Verify the final content */
    ret = mr_fs_open(&reader, "/app", MR_O_RDONLY);
    core_expect(__test_status, "open reader", ret, 0);
    if (ret == 0) {
        io_ret = mr_fs_read(&reader, buffer, sizeof(buffer));
        core_expect(__test_status, "read app", io_ret, 2);
        if ((io_ret == 2) && (mr_memcmp(buffer, "14", 2) != 0)) {
            MR_TEST_EXPECT_EQ_INT(mr_memcmp(buffer, "14", 2), 0);
        }
        core_expect(__test_status, "close reader", mr_fs_close(&reader), 0);
    }

    core_expect(__test_status, "close first append", mr_fs_close(&first), 0);
    core_expect(__test_status, "close second append", mr_fs_close(&second), 0);
    core_expect(__test_status, "unlink app", mr_fs_unlink("/app"), 0);
    core_expect(__test_status, "umount ramfs", mr_fs_umount("/"), 0);
    return;
_exit:
    if (first.inode) {
        core_expect(__test_status, "close first cleanup", mr_fs_close(&first),
                    0);
    }
    if (second.inode) {
        core_expect(__test_status, "close second cleanup", mr_fs_close(&second),
                    0);
    }
    if (reader.inode) {
        core_expect(__test_status, "close reader cleanup", mr_fs_close(&reader),
                    0);
    }
    core_expect(__test_status, "umount ramfs cleanup", mr_fs_umount("/"), 0);
    return;
}

MR_TEST(fs, core_rename_edges) {
    mr_file_t file = MR_FILE_INIT(file);
    mr_err_t ret;

    /* Mount RamFS */
    ret = fs_test_mount_root("ramfs", 0, MR_NULL);
    core_expect(__test_status, "mount ramfs", ret, 0);
    if (ret != 0) {
        return;
    }

    /* Source file and a source directory with a child */
    ret = mr_fs_open(&file, "/mv", MR_O_RDWR | MR_O_CREAT);
    core_expect(__test_status, "create mv", ret, 0);
    if (ret == 0) {
        core_expect(__test_status, "write mv", mr_fs_write(&file, "M", 1), 1);
        core_expect(__test_status, "close mv", mr_fs_close(&file), 0);
    }
    core_expect(__test_status, "mkdir mv-d", mr_fs_mkdir("/mv-d"), 0);
    ret = mr_fs_open(&file, "/mv-d/inside", MR_O_WRONLY | MR_O_CREAT);
    core_expect(__test_status, "create inside", ret, 0);
    if (ret == 0) {
        core_expect(__test_status, "close inside", mr_fs_close(&file), 0);
    }

    /* Structural rejections */
    core_expect(__test_status, "rename dot leaf", mr_fs_rename("/mv", "/mv/."),
                -MR_EINVAL);
    core_expect(__test_status, "rename root leaf", mr_fs_rename("/mv", "/"),
                -MR_EINVAL);
    core_expect(__test_status, "rename missing parent",
                mr_fs_rename("/mv", "/missing-dir/x"), -MR_ENOENT);
    core_expect(__test_status, "rename file over dir",
                mr_fs_rename("/mv", "/mv-d"), -MR_EEXIST);
    core_expect(__test_status, "rename dir over file",
                mr_fs_rename("/mv-d", "/mv"), -MR_EEXIST);

    /* The same file spelled differently is a no-op */
    core_expect(__test_status, "rename noop spelling",
                mr_fs_rename("/mv", "/./mv"), 0);
    core_stat_expect(__test_status, "/mv", 0, MR_INODE_REG);

    /* A nested mount is a different file system */
    core_expect(__test_status, "mkdir xmount", mr_fs_mkdir("/xmount"), 0);
    core_expect(__test_status, "mount xmount",
                mr_fs_mount(MR_NULL, "/xmount", "ramfs", 0, MR_NULL), 0);
    core_expect(__test_status, "rename across mounts",
                mr_fs_rename("/mv", "/xmount/mv"), -MR_EXDEV);
    core_stat_expect(__test_status, "/mv", 0, MR_INODE_REG);
    core_stat_expect(__test_status, "/xmount/mv", -MR_ENOENT, MR_INODE_REG);
    core_expect(__test_status, "umount xmount", mr_fs_umount("/xmount"), 0);
    core_expect(__test_status, "rmdir xmount", mr_fs_rmdir("/xmount"), 0);

    /* Clean up */
    core_expect(__test_status, "unlink inside", mr_fs_unlink("/mv-d/inside"),
                0);
    core_expect(__test_status, "rmdir mv-d", mr_fs_rmdir("/mv-d"), 0);
    core_expect(__test_status, "unlink mv", mr_fs_unlink("/mv"), 0);
    core_expect(__test_status, "umount ramfs", mr_fs_umount("/"), 0);
    return;
}

MR_TEST(fs, core_root_readdir) {
    mr_file_t file = MR_FILE_INIT(file);
    mr_file_t dir = MR_FILE_INIT(dir);
    mr_dirent_t dirent;
    mr_bool_t found_dir = MR_FALSE;
    mr_bool_t found_file = MR_FALSE;
    mr_bool_t found_extra = MR_FALSE;
    mr_err_t ret;

    /* Mount RamFS */
    ret = fs_test_mount_root("ramfs", 0, MR_NULL);
    core_expect(__test_status, "mount ramfs", ret, 0);
    if (ret != 0) {
        return;
    }

    /* Two root entries of different types */
    core_expect(__test_status, "mkdir r-dir", mr_fs_mkdir("/r-dir"), 0);
    ret = mr_fs_open(&file, "/r-file", MR_O_WRONLY | MR_O_CREAT);
    core_expect(__test_status, "create r-file", ret, 0);
    if (ret == 0) {
        core_expect(__test_status, "close r-file", mr_fs_close(&file), 0);
    }

    /* Open the root directory and enumerate everything */
    ret = mr_fs_open(&dir, "/", MR_O_RDONLY | MR_O_DIRECTORY);
    core_expect(__test_status, "opendir root", ret, 0);
    if (ret == 0) {
        while ((ret = mr_fs_readdir(&dir, &dirent)) > 0) {
            if (mr_strcmp(dirent.name, "r-dir") == 0) {
                found_dir = MR_TRUE;
                core_expect(__test_status, "r-dir type", dirent.type,
                            MR_INODE_DIR);
            } else if (mr_strcmp(dirent.name, "r-file") == 0) {
                found_file = MR_TRUE;
                core_expect(__test_status, "r-file type", dirent.type,
                            MR_INODE_REG);
            } else {
                found_extra = MR_TRUE;
            }
        }
        core_expect(__test_status, "readdir root eof", ret, 0);
        core_expect(__test_status, "found r-dir", found_dir, MR_TRUE);
        core_expect(__test_status, "found r-file", found_file, MR_TRUE);
        core_expect(__test_status, "no unexpected entries", found_extra,
                    MR_FALSE);
        core_expect(__test_status, "closedir root", mr_fs_close(&dir), 0);
    }

    /* Clean up */
    core_expect(__test_status, "unlink r-file", mr_fs_unlink("/r-file"), 0);
    core_expect(__test_status, "rmdir r-dir", mr_fs_rmdir("/r-dir"), 0);
    core_expect(__test_status, "umount ramfs", mr_fs_umount("/"), 0);
    return;
}

MR_TEST(fs, core_churn_heap) {
    mr_heap_info_t before;
    mr_heap_info_t after;
    mr_file_t file = MR_FILE_INIT(file);
    char path[64];
    int i;
    mr_err_t ret;

    /* Snapshot the heap before the test owns any allocation */
    before = mr_heap_information(MR_TRUE);
    ret = fs_test_mount_root("ramfs", 0, MR_NULL);
    core_expect(__test_status, "mount ramfs", ret, 0);
    if (ret != 0) {
        return;
    }

    /* Alternate short (inline) and long (heap) dentry names with files
     * and directories, removing everything in each cycle */
    for (i = 0; i < CORE_CHURN_CYCLES; i++) {
        mr_snprintf(path, sizeof(path), "/c-%d", i);
        ret = mr_fs_open(&file, path, MR_O_RDWR | MR_O_CREAT | MR_O_EXCL);
        core_expect(__test_status, "create churn short", ret, 0);
        if (ret != 0) {
            goto _exit;
        }
        core_expect(__test_status, "write churn short",
                    mr_fs_write(&file, "x", 1), 1);
        core_expect(__test_status, "close churn short", mr_fs_close(&file), 0);

        mr_snprintf(path, sizeof(path),
                    "/c-long-0123456789abcdef0123456789abcdef-%d", i);
        ret = mr_fs_open(&file, path, MR_O_RDWR | MR_O_CREAT | MR_O_EXCL);
        core_expect(__test_status, "create churn long", ret, 0);
        if (ret != 0) {
            goto _exit;
        }
        core_expect(__test_status, "close churn long", mr_fs_close(&file), 0);
        core_expect(__test_status, "unlink churn long", mr_fs_unlink(path), 0);

        mr_snprintf(path, sizeof(path), "/c-%d", i);
        core_expect(__test_status, "mkdir churn", mr_fs_mkdir("/c-dir"), 0);
        core_expect(__test_status, "rmdir churn", mr_fs_rmdir("/c-dir"), 0);
        core_expect(__test_status, "unlink churn short", mr_fs_unlink(path), 0);
    }

_exit:
    if (file.inode) {
        core_expect(__test_status, "close churn cleanup", mr_fs_close(&file),
                    0);
    }
    core_expect(__test_status, "umount ramfs", mr_fs_umount("/"), 0);
    after = mr_heap_information(MR_TRUE);
    core_expect(__test_status, "churn heap restored",
                after.used_bytes == before.used_bytes, MR_TRUE);
    core_expect(__test_status, "churn heap total kept",
                after.total_bytes == before.total_bytes, MR_TRUE);
    return;
}

#endif /* defined(MR_USE_TEST) && defined(MR_USE_FS) &&                        \
          defined(MR_USE_FS_RAMFS) */
