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
/* ==== from open_flags.c ==== */
static void open_flags_expect(volatile mr_uint32_t *__test_status,
                              const char *name, mr_int64_t actual,
                              mr_int64_t expected) {
    MR_IGNORE(name);

    MR_TEST_EXPECT_EQ_INT(actual, expected);
}

MR_TEST(fs, open_flags) {
    mr_file_t file = MR_FILE_INIT(file);
    mr_file_t other = MR_FILE_INIT(other);
    mr_stat_t stat;
    char buffer[8];
    mr_err_t ret;

    /* Mount RamFS */
    ret = fs_test_mount_root("ramfs", 0, MR_NULL);
    open_flags_expect(__test_status, "mount ramfs", ret, 0);
    if (ret != 0) {
        return;
    }

    /* O_CREAT creates a regular file */
    ret = mr_fs_open(&file, "/seed", MR_O_RDWR | MR_O_CREAT);
    open_flags_expect(__test_status, "create seed", ret, 0);
    if (ret == 0) {
        open_flags_expect(__test_status, "write seed",
                          mr_fs_write(&file, "abcd", 4), 4);
        open_flags_expect(__test_status, "close seed", mr_fs_close(&file), 0);
    }

    /* O_CREAT|O_EXCL rejects an existing path */
    open_flags_expect(
        __test_status, "excl existing",
        mr_fs_open(&file, "/seed", MR_O_RDWR | MR_O_CREAT | MR_O_EXCL),
        -MR_EEXIST);

    /* O_CREAT|O_EXCL succeeds for a new path */
    ret = mr_fs_open(&file, "/exclusive", MR_O_RDWR | MR_O_CREAT | MR_O_EXCL);
    open_flags_expect(__test_status, "excl create", ret, 0);
    if (ret == 0) {
        open_flags_expect(__test_status, "close exclusive", mr_fs_close(&file),
                          0);
    }

    /* Missing path without O_CREAT */
    open_flags_expect(__test_status, "missing no creat",
                      mr_fs_open(&file, "/missing", MR_O_RDONLY), -MR_ENOENT);

    /* O_CREAT|O_DIRECTORY cannot create a directory */
    open_flags_expect(
        __test_status, "creat directory",
        mr_fs_open(&file, "/newdir", MR_O_RDONLY | MR_O_CREAT | MR_O_DIRECTORY),
        -MR_ENOTDIR);

    /* O_DIRECTORY rejects a regular file */
    open_flags_expect(__test_status, "directory on file",
                      mr_fs_open(&file, "/seed", MR_O_RDONLY | MR_O_DIRECTORY),
                      -MR_ENOTDIR);

    /* O_DIRECTORY accepts a directory */
    open_flags_expect(__test_status, "mkdir flags-dir",
                      mr_fs_mkdir("/flags-dir"), 0);
    ret = mr_fs_open(&file, "/flags-dir", MR_O_RDONLY | MR_O_DIRECTORY);
    open_flags_expect(__test_status, "open directory flag", ret, 0);
    if (ret == 0) {
        open_flags_expect(__test_status, "close directory flag",
                          mr_fs_close(&file), 0);
    }

    /* Directory open for write is rejected */
    open_flags_expect(__test_status, "write directory",
                      mr_fs_open(&file, "/flags-dir", MR_O_WRONLY), -MR_EISDIR);
    open_flags_expect(__test_status, "rdwr directory",
                      mr_fs_open(&file, "/flags-dir", MR_O_RDWR), -MR_EISDIR);

    /* Access-mode checks on regular files */
    ret = mr_fs_open(&file, "/seed", MR_O_RDONLY);
    open_flags_expect(__test_status, "open readonly", ret, 0);
    if (ret == 0) {
        open_flags_expect(__test_status, "write readonly",
                          mr_fs_write(&file, "x", 1), -MR_EBADF);
        open_flags_expect(__test_status, "read readonly",
                          mr_fs_read(&file, buffer, sizeof(buffer)), 4);
        if (mr_memcmp(buffer, "abcd", 4) != 0) {
            MR_TEST_EXPECT_EQ_INT(mr_memcmp(buffer, "abcd", 4), 0);
        }
        open_flags_expect(__test_status, "close readonly", mr_fs_close(&file),
                          0);
    }

    ret = mr_fs_open(&file, "/seed", MR_O_WRONLY);
    open_flags_expect(__test_status, "open writeonly", ret, 0);
    if (ret == 0) {
        open_flags_expect(__test_status, "read writeonly",
                          mr_fs_read(&file, buffer, sizeof(buffer)), -MR_EBADF);
        open_flags_expect(__test_status, "write writeonly",
                          mr_fs_write(&file, "Z", 1), 1);
        open_flags_expect(__test_status, "close writeonly", mr_fs_close(&file),
                          0);
    }

    /* O_TRUNC zeros a writable regular file */
    ret = mr_fs_open(&file, "/seed", MR_O_RDWR | MR_O_TRUNC);
    open_flags_expect(__test_status, "open trunc", ret, 0);
    if (ret == 0) {
        open_flags_expect(__test_status, "stat trunc open",
                          mr_fs_fstat(&file, &stat), 0);
        open_flags_expect(__test_status, "trunc size zero", stat.size, 0);
        open_flags_expect(__test_status, "write after trunc",
                          mr_fs_write(&file, "xy", 2), 2);
        open_flags_expect(__test_status, "close trunc", mr_fs_close(&file), 0);
    }
    open_flags_expect(__test_status, "stat after trunc write",
                      fs_test_stat("/seed", &stat), 0);
    open_flags_expect(__test_status, "size after trunc write", stat.size, 2);

    /* O_TRUNC is ignored for a read-only open */
    ret = mr_fs_open(&file, "/seed", MR_O_RDONLY | MR_O_TRUNC);
    open_flags_expect(__test_status, "open trunc readonly", ret, 0);
    if (ret == 0) {
        open_flags_expect(__test_status, "stat trunc readonly",
                          mr_fs_fstat(&file, &stat), 0);
        open_flags_expect(__test_status, "trunc readonly keeps size", stat.size,
                          2);
        open_flags_expect(__test_status, "close trunc readonly",
                          mr_fs_close(&file), 0);
    }

    /* Invalid access mode: both WRONLY and RDWR bits set (ACCMODE == 3) */
    open_flags_expect(__test_status, "invalid accmode",
                      mr_fs_open(&file, "/seed", MR_O_WRONLY | MR_O_RDWR),
                      -MR_EINVAL);

    /* Zero-length read and write */
    ret = mr_fs_open(&file, "/seed", MR_O_RDWR);
    open_flags_expect(__test_status, "open zero io", ret, 0);
    if (ret == 0) {
        open_flags_expect(__test_status, "read zero",
                          mr_fs_read(&file, buffer, 0), 0);
        open_flags_expect(__test_status, "write zero",
                          mr_fs_write(&file, buffer, 0), 0);
        open_flags_expect(__test_status, "close zero io", mr_fs_close(&file),
                          0);
    }

    /* Directory cannot be read as a file */
    ret = mr_fs_open(&file, "/flags-dir", MR_O_RDONLY);
    open_flags_expect(__test_status, "open dir for read", ret, 0);
    if (ret == 0) {
        open_flags_expect(__test_status, "read directory",
                          mr_fs_read(&file, buffer, sizeof(buffer)),
                          -MR_EISDIR);
        open_flags_expect(__test_status, "close dir for read",
                          mr_fs_close(&file), 0);
    }

    /* Readdir rejects a regular file handle opened without O_DIRECTORY */
    ret = mr_fs_open(&other, "/seed", MR_O_RDONLY);
    open_flags_expect(__test_status, "open file readdir", ret, 0);
    if (ret == 0) {
        mr_dirent_t dirent;

        open_flags_expect(__test_status, "readdir on file",
                          mr_fs_readdir(&other, &dirent), -MR_ENOTDIR);
        open_flags_expect(__test_status, "close file readdir",
                          mr_fs_close(&other), 0);
    }

    open_flags_expect(__test_status, "umount ramfs", mr_fs_umount("/"), 0);
    return;
}
/* ==== from truncate.c ==== */
static void truncate_expect(volatile mr_uint32_t *__test_status,
                            const char *name, mr_int64_t actual,
                            mr_int64_t expected) {
    MR_IGNORE(name);

    MR_TEST_EXPECT_EQ_INT(actual, expected);
}

MR_TEST(fs, truncate) {
    mr_file_t file = MR_FILE_INIT(file);
    mr_file_t ro = MR_FILE_INIT(ro);
    mr_stat_t stat;
    char buffer[16];
    mr_ssize_t io_ret;
    mr_err_t ret;

    /* Mount RamFS */
    ret = fs_test_mount_root("ramfs", 0, MR_NULL);
    truncate_expect(__test_status, "mount ramfs", ret, 0);
    if (ret != 0) {
        return;
    }

    /* Seed a regular file */
    ret = mr_fs_open(&file, "/trunc", MR_O_RDWR | MR_O_CREAT);
    truncate_expect(__test_status, "create trunc file", ret, 0);
    if (ret != 0) {
        goto _exit;
    }
    truncate_expect(__test_status, "write seed",
                    mr_fs_write(&file, "0123456789", 10), 10);

    /* Same-size truncate is a no-op */
    truncate_expect(__test_status, "truncate same size",
                    mr_fs_ftruncate(&file, 10), 0);
    truncate_expect(__test_status, "stat same size", mr_fs_fstat(&file, &stat),
                    0);
    truncate_expect(__test_status, "same size value", stat.size, 10);

    /* Shrink */
    truncate_expect(__test_status, "truncate shrink", mr_fs_ftruncate(&file, 4),
                    0);
    truncate_expect(__test_status, "stat shrink", mr_fs_fstat(&file, &stat), 0);
    truncate_expect(__test_status, "shrink size", stat.size, 4);
    truncate_expect(__test_status, "seek shrink head",
                    mr_fs_lseek(&file, 0, MR_SEEK_SET), 0);
    io_ret = mr_fs_read(&file, buffer, sizeof(buffer));
    truncate_expect(__test_status, "read shrink", io_ret, 4);
    if ((io_ret == 4) && (mr_memcmp(buffer, "0123", 4) != 0)) {
        MR_TEST_EXPECT_EQ_INT(mr_memcmp(buffer, "0123", 4), 0);
    }

    /* Grow with zero fill */
    truncate_expect(__test_status, "truncate grow", mr_fs_ftruncate(&file, 8),
                    0);
    truncate_expect(__test_status, "stat grow", mr_fs_fstat(&file, &stat), 0);
    truncate_expect(__test_status, "grow size", stat.size, 8);
    truncate_expect(__test_status, "seek grow head",
                    mr_fs_lseek(&file, 0, MR_SEEK_SET), 0);
    io_ret = mr_fs_read(&file, buffer, sizeof(buffer));
    truncate_expect(__test_status, "read grow", io_ret, 8);
    if ((io_ret == 8) && (mr_memcmp(buffer, "0123\0\0\0\0", 8) != 0)) {
        MR_TEST_EXPECT_EQ_INT(mr_memcmp(buffer, "0123\0\0\0\0", 8), 0);
    }

    /* Truncate to zero */
    truncate_expect(__test_status, "truncate zero", mr_fs_ftruncate(&file, 0),
                    0);
    truncate_expect(__test_status, "stat zero", mr_fs_fstat(&file, &stat), 0);
    truncate_expect(__test_status, "zero size", stat.size, 0);
    truncate_expect(__test_status, "seek zero head",
                    mr_fs_lseek(&file, 0, MR_SEEK_SET), 0);
    truncate_expect(__test_status, "read empty",
                    mr_fs_read(&file, buffer, sizeof(buffer)), 0);

    /* Hole write beyond EOF zero-fills the gap */
    truncate_expect(__test_status, "seek hole",
                    mr_fs_lseek(&file, 4, MR_SEEK_SET), 4);
    truncate_expect(__test_status, "write hole", mr_fs_write(&file, "AB", 2),
                    2);
    truncate_expect(__test_status, "stat hole", mr_fs_fstat(&file, &stat), 0);
    truncate_expect(__test_status, "hole size", stat.size, 6);
    truncate_expect(__test_status, "seek hole head",
                    mr_fs_lseek(&file, 0, MR_SEEK_SET), 0);
    io_ret = mr_fs_read(&file, buffer, sizeof(buffer));
    truncate_expect(__test_status, "read hole", io_ret, 6);
    if ((io_ret == 6) && (mr_memcmp(buffer, "\0\0\0\0AB", 6) != 0)) {
        MR_TEST_EXPECT_EQ_INT(mr_memcmp(buffer, "\0\0\0\0AB", 6), 0);
    }

    /* Negative length is invalid */
    truncate_expect(__test_status, "truncate negative",
                    mr_fs_ftruncate(&file, -1), -MR_EINVAL);

    /* Truncate does not change the file position */
    truncate_expect(__test_status, "seek mid",
                    mr_fs_lseek(&file, 2, MR_SEEK_SET), 2);
    truncate_expect(__test_status, "truncate keep pos size",
                    mr_fs_ftruncate(&file, 3), 0);
    truncate_expect(__test_status, "position after truncate",
                    mr_fs_lseek(&file, 0, MR_SEEK_CUR), 2);

    truncate_expect(__test_status, "close trunc file", mr_fs_close(&file), 0);

    /* Read-only handle cannot truncate */
    ret = mr_fs_open(&ro, "/trunc", MR_O_RDONLY);
    truncate_expect(__test_status, "open readonly trunc", ret, 0);
    if (ret == 0) {
        truncate_expect(__test_status, "truncate readonly",
                        mr_fs_ftruncate(&ro, 1), -MR_EBADF);
        truncate_expect(__test_status, "close readonly trunc", mr_fs_close(&ro),
                        0);
    }

    /* Directory truncate is rejected */
    truncate_expect(__test_status, "mkdir trunc-dir", mr_fs_mkdir("/trunc-dir"),
                    0);
    ret = mr_fs_open(&file, "/trunc-dir", MR_O_RDONLY);
    truncate_expect(__test_status, "open trunc-dir", ret, 0);
    if (ret == 0) {
        truncate_expect(__test_status, "truncate directory",
                        mr_fs_ftruncate(&file, 0), -MR_EISDIR);
        truncate_expect(__test_status, "close trunc-dir", mr_fs_close(&file),
                        0);
    }

    /* Closed handle */
    truncate_expect(__test_status, "truncate closed", mr_fs_ftruncate(&file, 0),
                    -MR_EBADF);

    /* Seek edges on an empty file */
    ret = mr_fs_open(&file, "/seek-edge", MR_O_RDWR | MR_O_CREAT);
    truncate_expect(__test_status, "create seek-edge", ret, 0);
    if (ret == 0) {
        truncate_expect(__test_status, "seek end empty",
                        mr_fs_lseek(&file, 0, MR_SEEK_END), 0);
        truncate_expect(__test_status, "seek cur empty",
                        mr_fs_lseek(&file, 0, MR_SEEK_CUR), 0);
        truncate_expect(__test_status, "seek invalid whence",
                        mr_fs_lseek(&file, 0, 99), -MR_EINVAL);
        truncate_expect(__test_status, "seek negative rejected",
                        mr_fs_lseek(&file, -5, MR_SEEK_SET), -MR_EINVAL);
        truncate_expect(__test_status, "close seek-edge", mr_fs_close(&file),
                        0);
    }

_exit:
    truncate_expect(__test_status, "umount ramfs", mr_fs_umount("/"), 0);
    return;
}
/* ==== from relative.c ==== */
static void fs_relative_expect(volatile mr_uint32_t *__test_status,
                               const char *name, mr_int64_t actual,
                               mr_int64_t expected) {
    MR_IGNORE(name);

    MR_TEST_EXPECT_EQ_INT(actual, expected);
}

MR_TEST(fs, relative) {
    mr_file_t file = MR_FILE_INIT(file);
    char cwd[32];
    char cwd_exact[6];
    char cwd_short[5] = {'X', 'X', 'X', 'X', '\0'};
    char root_exact[2];
    char root_short[1] = {'X'};
    mr_stat_t stat;
    mr_err_t ret;

    /* Mount RamFS */
    ret = fs_test_mount_root("ramfs", 0, MR_NULL);
    fs_relative_expect(__test_status, "mount ramfs", ret, 0);
    if (ret != 0) {
        return;
    }

    /* Select a working directory */
    fs_relative_expect(__test_status, "mkdir base", mr_fs_mkdir("/base"), 0);
    fs_relative_expect(__test_status, "chdir base", mr_fs_chdir("/base"), 0);
    fs_relative_expect(__test_status, "getcwd base",
                       mr_fs_getcwd(cwd, sizeof(cwd)), 5);
    if (mr_strcmp(cwd, "/base") != 0) {
        MR_TEST_EXPECT_EQ_INT(mr_strcmp(cwd, "/base"), 0);
    }
    fs_relative_expect(__test_status, "getcwd exact size",
                       mr_fs_getcwd(cwd_exact, sizeof(cwd_exact)), 5);
    if (mr_strcmp(cwd_exact, "/base") != 0) {
        MR_TEST_EXPECT_EQ_INT(mr_strcmp(cwd_exact, "/base"), 0);
    }
    fs_relative_expect(__test_status, "getcwd short size",
                       mr_fs_getcwd(cwd_short, sizeof(cwd_short)), -MR_ERANGE);
    fs_relative_expect(__test_status, "getcwd short keeps data", cwd_short[0],
                       'X');

    /* Relative file operations use CWD */
    ret = mr_fs_open(&file, "file", MR_O_RDWR | MR_O_CREAT);
    fs_relative_expect(__test_status, "create relative file", ret, 0);
    if (ret == 0) {
        fs_relative_expect(__test_status, "write relative file",
                           mr_fs_write(&file, "data", 4), 4);
        fs_relative_expect(__test_status, "close relative file",
                           mr_fs_close(&file), 0);
    }
    fs_relative_expect(__test_status, "stat relative file",
                       fs_test_stat("/base/file", &stat), 0);
    fs_relative_expect(__test_status, "rename relative file",
                       mr_fs_rename("file", "renamed"), 0);
    fs_relative_expect(__test_status, "old relative name absent",
                       fs_test_stat("/base/file", &stat), -MR_ENOENT);

    /* Dot, dot-dot and repeated separators are normalized during lookup */
    fs_relative_expect(__test_status, "mkdir relative directory",
                       mr_fs_mkdir("dir"), 0);
    ret = mr_fs_open(&file, "dir/../renamed", MR_O_RDONLY);
    fs_relative_expect(__test_status, "open through dot-dot", ret, 0);
    if (ret == 0) {
        fs_relative_expect(__test_status, "close dot-dot file",
                           mr_fs_close(&file), 0);
    }
    fs_relative_expect(__test_status, "mkdir missing repeated parent",
                       mr_fs_mkdir("repeat//nested"), -MR_ENOENT);
    fs_relative_expect(__test_status, "mkdir repeated base",
                       mr_fs_mkdir("repeat"), 0);
    fs_relative_expect(__test_status, "mkdir repeated nested",
                       mr_fs_mkdir("repeat//nested"), 0);

    /* CWD can move with relative paths */
    fs_relative_expect(__test_status, "chdir relative directory",
                       mr_fs_chdir("dir"), 0);
    fs_relative_expect(__test_status, "getcwd relative directory",
                       mr_fs_getcwd(cwd, sizeof(cwd)), 9);
    if (mr_strcmp(cwd, "/base/dir") != 0) {
        MR_TEST_EXPECT_EQ_INT(mr_strcmp(cwd, "/base/dir"), 0);
    }
    fs_relative_expect(__test_status, "chdir parent", mr_fs_chdir(".."), 0);

    /* Remove relative namespace entries */
    fs_relative_expect(__test_status, "unlink relative file",
                       mr_fs_unlink("renamed"), 0);
    fs_relative_expect(__test_status, "rmdir relative directory",
                       mr_fs_rmdir("dir"), 0);
    fs_relative_expect(__test_status, "rmdir repeated nested",
                       mr_fs_rmdir("repeat/nested"), 0);
    fs_relative_expect(__test_status, "rmdir repeated base",
                       mr_fs_rmdir("repeat"), 0);
    fs_relative_expect(__test_status, "chdir root", mr_fs_chdir("/"), 0);
    fs_relative_expect(__test_status, "getcwd root exact size",
                       mr_fs_getcwd(root_exact, sizeof(root_exact)), 1);
    if (mr_strcmp(root_exact, "/") != 0) {
        MR_TEST_EXPECT_EQ_INT(mr_strcmp(root_exact, "/"), 0);
    }
    fs_relative_expect(__test_status, "getcwd root short size",
                       mr_fs_getcwd(root_short, sizeof(root_short)), -MR_ERANGE);
    fs_relative_expect(__test_status, "getcwd root short keeps data",
                       root_short[0], 'X');
    fs_relative_expect(__test_status, "rmdir base", mr_fs_rmdir("/base"), 0);
    fs_relative_expect(__test_status, "umount ramfs", mr_fs_umount("/"), 0);
    return;
}
/* ==== from rename_cwd.c ==== */
static void rename_cwd_expect(volatile mr_uint32_t *__test_status,
                              const char *name, mr_int64_t actual,
                              mr_int64_t expected) {
    MR_IGNORE(name);

    MR_TEST_EXPECT_EQ_INT(actual, expected);
}

static void rename_cwd_expect_path(volatile mr_uint32_t *__test_status,
                                   const char *name, const char *expected) {
    char cwd[32];
    mr_ssize_t ret;

    ret = mr_fs_getcwd(cwd, sizeof(cwd));
    rename_cwd_expect(__test_status, name, ret,
                      (mr_ssize_t)mr_strlen(expected));
    if ((ret >= 0) && (mr_strcmp(cwd, expected) != 0)) {
        MR_TEST_EXPECT_EQ_INT(mr_strcmp(cwd, expected), 0);
    }
}

/* Test: rename an active ancestor without invalidating CWD or open files */
MR_TEST(fs, rename_cwd) {
    mr_file_t file = MR_FILE_INIT(file);
    mr_stat_t stat;
    char buffer[8];
    mr_ssize_t io_ret;
    mr_err_t ret;
    mr_bool_t mounted = MR_FALSE;

    /* Mount RamFS and prepare two namespace branches */
    ret = fs_test_mount_root("ramfs", 0, MR_NULL);
    rename_cwd_expect(__test_status, "mount ramfs", ret, 0);
    if (ret != 0) {
        return;
    }
    mounted = MR_TRUE;
    ret = mr_fs_mkdir("/tree");
    rename_cwd_expect(__test_status, "mkdir tree", ret, 0);
    if (ret != 0) {
        goto _exit;
    }
    ret = mr_fs_mkdir("/tree/child");
    rename_cwd_expect(__test_status, "mkdir child", ret, 0);
    if (ret != 0) {
        goto _exit;
    }
    ret = mr_fs_mkdir("/outside");
    rename_cwd_expect(__test_status, "mkdir outside", ret, 0);
    if (ret != 0) {
        goto _exit;
    }

    /* Keep an open file inside the active directory */
    ret = mr_fs_chdir("/tree/child");
    rename_cwd_expect(__test_status, "chdir child", ret, 0);
    if (ret != 0) {
        goto _exit;
    }
    rename_cwd_expect_path(__test_status, "cwd before rename", "/tree/child");
    ret = mr_fs_open(&file, "active", MR_O_RDWR | MR_O_CREAT);
    rename_cwd_expect(__test_status, "create active", ret, 0);
    if (ret != 0) {
        goto _exit;
    }
    rename_cwd_expect(__test_status, "write active",
                      mr_fs_write(&file, "seed", 4), 4);

    /* Moving an ancestor updates the visible CWD without losing its identity */
    rename_cwd_expect(__test_status, "rename active ancestor",
                      mr_fs_rename("/tree", "/renamed"), 0);
    rename_cwd_expect_path(__test_status, "cwd after ancestor rename",
                           "/renamed/child");

    /* Move the open file out of CWD and retain access through its handle */
    rename_cwd_expect(__test_status, "move open file",
                      mr_fs_rename("active", "/outside/moved"), 0);
    rename_cwd_expect(__test_status, "old active name absent",
                      fs_test_stat("active", &stat), -MR_ENOENT);
    rename_cwd_expect(__test_status, "moved file present",
                      fs_test_stat("/outside/moved", &stat), 0);
    rename_cwd_expect_path(__test_status, "cwd after file move",
                           "/renamed/child");
    rename_cwd_expect(__test_status, "seek moved open file",
                      mr_fs_lseek(&file, 0, MR_SEEK_SET), 0);
    io_ret = mr_fs_read(&file, buffer, sizeof(buffer));
    rename_cwd_expect(__test_status, "read moved open file", io_ret, 4);
    if ((io_ret == 4) && (mr_memcmp(buffer, "seed", 4) != 0)) {
        MR_TEST_EXPECT_EQ_INT(mr_memcmp(buffer, "seed", 4), 0);
    }
    rename_cwd_expect(__test_status, "close moved open file",
                      mr_fs_close(&file), 0);

    /* Remove the moved namespace while preserving normal cleanup order */
    rename_cwd_expect(__test_status, "chdir root", mr_fs_chdir("/"), 0);
    rename_cwd_expect(__test_status, "rmdir moved child",
                      mr_fs_rmdir("/renamed/child"), 0);
    rename_cwd_expect(__test_status, "rmdir renamed tree",
                      mr_fs_rmdir("/renamed"), 0);
    rename_cwd_expect(__test_status, "unlink moved file",
                      mr_fs_unlink("/outside/moved"), 0);
    rename_cwd_expect(__test_status, "rmdir outside", mr_fs_rmdir("/outside"),
                      0);

_exit:
    if (file.inode) {
        rename_cwd_expect(__test_status, "close active on cleanup",
                          mr_fs_close(&file), 0);
    }
    if (mounted) {
        rename_cwd_expect(__test_status, "cleanup chdir root", mr_fs_chdir("/"),
                          0);
        rename_cwd_expect(__test_status, "umount ramfs", mr_fs_umount("/"), 0);
    }
}
/* ==== from path_resolve.c ==== */
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
/* ==== from lifecycle_edges.c ==== */
static void lifecycle_edges_expect(volatile mr_uint32_t *__test_status,
                                   const char *name, mr_int64_t actual,
                                   mr_int64_t expected) {
    MR_IGNORE(name);

    MR_TEST_EXPECT_EQ_INT(actual, expected);
}

MR_TEST(fs, lifecycle_edges) {
    mr_file_t held = MR_FILE_INIT(held);
    mr_file_t replacement = MR_FILE_INIT(replacement);
    mr_stat_t stat;
    char buffer[8];
    mr_ssize_t io_ret;
    mr_err_t ret;

    /* Mount RamFS */
    ret = fs_test_mount_root("ramfs", 0, MR_NULL);
    lifecycle_edges_expect(__test_status, "mount ramfs", ret, 0);
    if (ret != 0) {
        return;
    }

    /* An unlinked open file remains usable until its last handle is closed */
    ret = mr_fs_open(&held, "/held", MR_O_RDWR | MR_O_CREAT);
    lifecycle_edges_expect(__test_status, "create held file", ret, 0);
    if (ret == 0) {
        lifecycle_edges_expect(__test_status, "write held file",
                               mr_fs_write(&held, "old", 3), 3);
        lifecycle_edges_expect(__test_status, "unlink held file",
                               mr_fs_unlink("/held"), 0);
        lifecycle_edges_expect(__test_status, "held path absent",
                               fs_test_stat("/held", &stat), -MR_ENOENT);
    }

    /* Recreating the pathname must produce a separate inode and data stream */
    ret = mr_fs_open(&replacement, "/held", MR_O_RDWR | MR_O_CREAT | MR_O_EXCL);
    lifecycle_edges_expect(__test_status, "recreate held path", ret, 0);
    if (ret == 0) {
        lifecycle_edges_expect(__test_status, "write replacement",
                               mr_fs_write(&replacement, "new", 3), 3);
    }

    if (held.inode) {
        lifecycle_edges_expect(__test_status, "seek unlinked held",
                               mr_fs_lseek(&held, 0, MR_SEEK_SET), 0);
        io_ret = mr_fs_read(&held, buffer, sizeof(buffer));
        lifecycle_edges_expect(__test_status, "read unlinked held", io_ret, 3);
        if ((io_ret == 3) && (mr_memcmp(buffer, "old", 3) != 0)) {
            MR_TEST_EXPECT_EQ_INT(mr_memcmp(buffer, "old", 3), 0);
        }
        lifecycle_edges_expect(__test_status, "close unlinked held",
                               mr_fs_close(&held), 0);
    }
    if (replacement.inode) {
        lifecycle_edges_expect(__test_status, "seek replacement",
                               mr_fs_lseek(&replacement, 0, MR_SEEK_SET), 0);
        io_ret = mr_fs_read(&replacement, buffer, sizeof(buffer));
        lifecycle_edges_expect(__test_status, "read replacement", io_ret, 3);
        if ((io_ret == 3) && (mr_memcmp(buffer, "new", 3) != 0)) {
            MR_TEST_EXPECT_EQ_INT(mr_memcmp(buffer, "new", 3), 0);
        }
        lifecycle_edges_expect(__test_status, "close replacement",
                               mr_fs_close(&replacement), 0);
    }

    lifecycle_edges_expect(__test_status, "unlink replacement",
                           mr_fs_unlink("/held"), 0);
    lifecycle_edges_expect(__test_status, "umount ramfs", mr_fs_umount("/"), 0);
    return;
}
/* ==== from io_errors.c ==== */
static void io_errors_expect(volatile mr_uint32_t *__test_status,
                             const char *name, mr_int64_t actual,
                             mr_int64_t expected) {
    MR_IGNORE(name);

    MR_TEST_EXPECT_EQ_INT(actual, expected);
}

MR_TEST(fs, io_errors) {
    mr_file_t file = MR_FILE_INIT(file);
    mr_file_t dir = MR_FILE_INIT(dir);
    char buffer[8];
    mr_err_t ret;

    /* Mount RamFS */
    ret = fs_test_mount_root("ramfs", 0, MR_NULL);
    io_errors_expect(__test_status, "mount ramfs", ret, 0);
    if (ret != 0) {
        return;
    }

    /* Seed nodes */
    ret = mr_fs_open(&file, "/io", MR_O_RDWR | MR_O_CREAT);
    io_errors_expect(__test_status, "create io", ret, 0);
    if (ret == 0) {
        io_errors_expect(__test_status, "write io seed",
                         mr_fs_write(&file, "abc", 3), 3);
        io_errors_expect(__test_status, "close io", mr_fs_close(&file), 0);
    }
    io_errors_expect(__test_status, "mkdir iodir", mr_fs_mkdir("/iodir"), 0);

    /* Read on write-only handle */
    ret = mr_fs_open(&file, "/io", MR_O_WRONLY);
    io_errors_expect(__test_status, "open writeonly io", ret, 0);
    if (ret == 0) {
        io_errors_expect(__test_status, "read writeonly",
                         mr_fs_read(&file, buffer, 1), -MR_EBADF);
        io_errors_expect(__test_status, "close writeonly io",
                         mr_fs_close(&file), 0);
    }

    /* Write on read-only handle */
    ret = mr_fs_open(&file, "/io", MR_O_RDONLY);
    io_errors_expect(__test_status, "open readonly io", ret, 0);
    if (ret == 0) {
        io_errors_expect(__test_status, "write readonly",
                         mr_fs_write(&file, "x", 1), -MR_EBADF);
        io_errors_expect(__test_status, "close readonly io", mr_fs_close(&file),
                         0);
    }

    /* Write / read on a directory handle. A directory is always opened
     * read-only, so write hits the read-only check(-MR_EBADF) before the
     * directory check, while read hits the directory check */
    ret = mr_fs_open(&dir, "/iodir", MR_O_RDONLY);
    io_errors_expect(__test_status, "open iodir", ret, 0);
    if (ret == 0) {
        io_errors_expect(__test_status, "write directory",
                         mr_fs_write(&dir, "x", 1), -MR_EBADF);
        io_errors_expect(__test_status, "read directory",
                         mr_fs_read(&dir, buffer, 1), -MR_EISDIR);
        io_errors_expect(__test_status, "close iodir", mr_fs_close(&dir), 0);
    }

    /* Operations on a closed handle */
    io_errors_expect(__test_status, "seek closed",
                     mr_fs_lseek(&file, 1, MR_SEEK_SET), -MR_EBADF);
    io_errors_expect(__test_status, "position closed",
                     mr_fs_lseek(&file, 0, MR_SEEK_CUR), -MR_EBADF);
    io_errors_expect(__test_status, "truncate closed", mr_fs_ftruncate(&file, 1),
                     -MR_EBADF);

    /* getcwd buffer too small at root */
    io_errors_expect(__test_status, "chdir root", mr_fs_chdir("/"), 0);
    io_errors_expect(__test_status, "getcwd size1", mr_fs_getcwd(buffer, 1),
                     -MR_ERANGE);
    io_errors_expect(__test_status, "getcwd root", mr_fs_getcwd(buffer, 2), 1);
    if (buffer[0] != '/') {
        MR_TEST_EXPECT_EQ_INT(buffer[0], '/');
    }

    io_errors_expect(__test_status, "umount ramfs", mr_fs_umount("/"), 0);
    return;
}
/* ==== from sync_ioctl.c ==== */
/* Mock FS with sync/ioctl hooks */
static mr_size_t __sync_calls;
static mr_size_t __ioctl_calls;
static int __last_ioctl_cmd;
static void *__last_ioctl_arg;
static mr_err_t __sync_ret;

static mr_err_t syncfs_inode_create(mr_inode_t *dir, const char *name,
                                    mr_inode_type_t type, mr_inode_t **out);
static mr_err_t syncfs_file_sync(mr_file_t *file);
static mr_err_t syncfs_file_ioctl(mr_file_t *file, int cmd, void *arg);
static mr_err_t syncfs_mount(mr_sblock_t *sb, const char *source,
                             int flags, void *data);

static const mr_inode_ops_t __syncfs_inode_ops = {
    .create = syncfs_inode_create,
};

static const mr_file_ops_t __syncfs_file_ops = {
    .fsync = syncfs_file_sync,
    .ioctl = syncfs_file_ioctl,
};

static mr_file_system_t __syncfs
    = MR_FILE_SYSTEM_INIT("syncfs", syncfs_mount, MR_NULL);

static void sync_ioctl_expect(volatile mr_uint32_t *__test_status,
                              const char *name, mr_int64_t actual,
                              mr_int64_t expected) {
    MR_IGNORE(name);

    MR_TEST_EXPECT_EQ_INT(actual, expected);
}

static mr_err_t syncfs_inode_create(mr_inode_t *dir, const char *name,
                                    mr_inode_type_t type, mr_inode_t **out) {
    MR_IGNORE(dir);
    MR_IGNORE(name);

    *out = mr_inode_create(type, &__syncfs_inode_ops, &__syncfs_file_ops,
                           MR_NULL);
    return (*out) ? 0 : -MR_ENOMEM;
}

static mr_err_t syncfs_file_sync(mr_file_t *file) {
    MR_IGNORE(file);

    __sync_calls++;
    return __sync_ret;
}

static mr_err_t syncfs_file_ioctl(mr_file_t *file, int cmd, void *arg) {
    MR_IGNORE(file);

    __ioctl_calls++;
    __last_ioctl_cmd = cmd;
    __last_ioctl_arg = arg;
    return (cmd == 0x55) ? 0 : -MR_EINVAL;
}

static mr_err_t syncfs_mount(mr_sblock_t *sb, const char *source,
                             int flags, void *data) {
    MR_IGNORE(source);
    MR_IGNORE(flags);
    MR_IGNORE(data);

    sb->root = mr_inode_create(MR_INODE_DIR, &__syncfs_inode_ops,
                               &__syncfs_file_ops, MR_NULL);
    return sb->root ? 0 : -MR_ENOMEM;
}

MR_TEST(fs, sync_ioctl) {
    mr_file_t file = MR_FILE_INIT(file);
    int arg = 7;
    mr_err_t ret;

    /* RamFS has no sync/ioctl hooks */
    ret = fs_test_mount_root("ramfs", 0, MR_NULL);
    sync_ioctl_expect(__test_status, "mount ramfs", ret, 0);
    if (ret != 0) {
        return;
    }

    ret = mr_fs_open(&file, "/nosync", MR_O_RDWR | MR_O_CREAT);
    sync_ioctl_expect(__test_status, "create nosync", ret, 0);
    if (ret == 0) {
        sync_ioctl_expect(__test_status, "ramfs sync", mr_fs_fsync(&file),
                          -MR_ENOSYS);
        sync_ioctl_expect(__test_status, "ramfs ioctl",
                          mr_fs_ioctl(&file, 1, MR_NULL), -MR_ENOSYS);
        sync_ioctl_expect(__test_status, "close nosync", mr_fs_close(&file), 0);
    }
    sync_ioctl_expect(__test_status, "umount ramfs", mr_fs_umount("/"), 0);

    /* Closed-handle rejection */
    sync_ioctl_expect(__test_status, "sync closed", mr_fs_fsync(&file),
                      -MR_EBADF);
    sync_ioctl_expect(__test_status, "ioctl closed",
                      mr_fs_ioctl(&file, 1, MR_NULL), -MR_EBADF);

    /* Register and exercise a FS that implements both hooks */
    ret = mr_file_system_register(&__syncfs);
    if ((ret != 0) && (ret != -MR_EEXIST)) {
        sync_ioctl_expect(__test_status, "register syncfs", ret, 0);
        return;
    }

    __sync_calls = 0;
    __ioctl_calls = 0;
    __last_ioctl_cmd = 0;
    __last_ioctl_arg = MR_NULL;
    __sync_ret = 0;

    ret = fs_test_mount_root("syncfs", 0, MR_NULL);
    sync_ioctl_expect(__test_status, "mount syncfs", ret, 0);
    if (ret != 0) {
        return;
    }

    ret = mr_fs_open(&file, "/hooked", MR_O_RDWR | MR_O_CREAT);
    sync_ioctl_expect(__test_status, "create hooked", ret, 0);
    if (ret == 0) {
        sync_ioctl_expect(__test_status, "sync hooked", mr_fs_fsync(&file), 0);
        sync_ioctl_expect(__test_status, "sync call count",
                          (mr_int64_t)__sync_calls, 1);
        __sync_ret = -MR_EIO;
        sync_ioctl_expect(__test_status, "sync error", mr_fs_fsync(&file),
                          -MR_EIO);
        sync_ioctl_expect(__test_status, "sync error call count",
                          (mr_int64_t)__sync_calls, 2);
        __sync_ret = 0;
        sync_ioctl_expect(__test_status, "ioctl good",
                          mr_fs_ioctl(&file, 0x55, &arg), 0);
        sync_ioctl_expect(__test_status, "ioctl call count",
                          (mr_int64_t)__ioctl_calls, 1);
        sync_ioctl_expect(__test_status, "ioctl cmd", __last_ioctl_cmd, 0x55);
        if (__last_ioctl_arg != &arg) {
            MR_TEST_EXPECT_EQ_PTR(__last_ioctl_arg, &arg);
        }
        sync_ioctl_expect(__test_status, "ioctl bad cmd",
                          mr_fs_ioctl(&file, 0, MR_NULL), -MR_EINVAL);
        sync_ioctl_expect(__test_status, "close hooked", mr_fs_close(&file), 0);
    }

    sync_ioctl_expect(__test_status, "umount syncfs", mr_fs_umount("/"), 0);
    return;
}

#endif /* defined(MR_USE_TEST) && defined(MR_USE_FS) */
