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

#endif /* defined(MR_USE_TEST) && defined(MR_USE_FS) */
