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

static int open_flags_expect(const char *name, mr_int64_t actual,
                             mr_int64_t expected) {
    MR_IGNORE(name);

    MR_TEST_EXPECT_EQ_INT(actual, expected);
    return (actual == expected) ? 0 : 1;
}

static int fs_open_flags_test(void) {
    mr_file_t file = MR_FILE_INIT(file);
    mr_file_t other = MR_FILE_INIT(other);
    mr_stat_t stat;
    char buffer[8];
    int failures = 0;
    mr_err_t ret;

    /* Mount RamFS */
    ret = mr_fs_mount(MR_NULL, "/", "ramfs", 0, MR_NULL);
    failures += open_flags_expect("mount ramfs", ret, 0);
    if (ret != 0) {
        return failures;
    }

    /* O_CREAT creates a regular file */
    ret = mr_fs_open(&file, "/seed", MR_O_RDWR | MR_O_CREAT);
    failures += open_flags_expect("create seed", ret, 0);
    if (ret == 0) {
        failures += open_flags_expect("write seed",
                                     mr_fs_write(&file, "abcd", 4), 4);
        failures += open_flags_expect("close seed", mr_fs_close(&file), 0);
    }

    /* O_CREAT|O_EXCL rejects an existing path */
    failures += open_flags_expect(
        "excl existing",
        mr_fs_open(&file, "/seed", MR_O_RDWR | MR_O_CREAT | MR_O_EXCL),
        -MR_EEXIST);

    /* O_CREAT|O_EXCL succeeds for a new path */
    ret = mr_fs_open(&file, "/exclusive",
                     MR_O_RDWR | MR_O_CREAT | MR_O_EXCL);
    failures += open_flags_expect("excl create", ret, 0);
    if (ret == 0) {
        failures
            += open_flags_expect("close exclusive", mr_fs_close(&file), 0);
    }

    /* Missing path without O_CREAT */
    failures += open_flags_expect(
        "missing no creat", mr_fs_open(&file, "/missing", MR_O_RDONLY),
        -MR_ENOENT);

    /* O_CREAT|O_DIRECTORY cannot create a directory */
    failures += open_flags_expect(
        "creat directory",
        mr_fs_open(&file, "/newdir", MR_O_RDONLY | MR_O_CREAT | MR_O_DIRECTORY),
        -MR_ENOTDIR);

    /* O_DIRECTORY rejects a regular file */
    failures += open_flags_expect(
        "directory on file",
        mr_fs_open(&file, "/seed", MR_O_RDONLY | MR_O_DIRECTORY), -MR_ENOTDIR);

    /* O_DIRECTORY accepts a directory */
    failures += open_flags_expect("mkdir flags-dir", mr_fs_mkdir("/flags-dir"),
                                  0);
    ret = mr_fs_open(&file, "/flags-dir", MR_O_RDONLY | MR_O_DIRECTORY);
    failures += open_flags_expect("open directory flag", ret, 0);
    if (ret == 0) {
        failures
            += open_flags_expect("close directory flag", mr_fs_close(&file), 0);
    }

    /* Directory open for write is rejected */
    failures += open_flags_expect(
        "write directory", mr_fs_open(&file, "/flags-dir", MR_O_WRONLY),
        -MR_EISDIR);
    failures += open_flags_expect(
        "rdwr directory", mr_fs_open(&file, "/flags-dir", MR_O_RDWR),
        -MR_EISDIR);

    /* Access-mode checks on regular files */
    ret = mr_fs_open(&file, "/seed", MR_O_RDONLY);
    failures += open_flags_expect("open readonly", ret, 0);
    if (ret == 0) {
        failures += open_flags_expect(
            "write readonly", mr_fs_write(&file, "x", 1), -MR_EBADF);
        failures += open_flags_expect(
            "read readonly", mr_fs_read(&file, buffer, sizeof(buffer)), 4);
        if (mr_memcmp(buffer, "abcd", 4) != 0) {
            MR_TEST_EXPECT_EQ_INT(mr_memcmp(buffer, "abcd", 4), 0);
            failures++;
        }
        failures
            += open_flags_expect("close readonly", mr_fs_close(&file), 0);
    }

    ret = mr_fs_open(&file, "/seed", MR_O_WRONLY);
    failures += open_flags_expect("open writeonly", ret, 0);
    if (ret == 0) {
        failures += open_flags_expect(
            "read writeonly", mr_fs_read(&file, buffer, sizeof(buffer)),
            -MR_EBADF);
        failures += open_flags_expect("write writeonly",
                                      mr_fs_write(&file, "Z", 1), 1);
        failures
            += open_flags_expect("close writeonly", mr_fs_close(&file), 0);
    }

    /* O_TRUNC zeros a writable regular file */
    ret = mr_fs_open(&file, "/seed", MR_O_RDWR | MR_O_TRUNC);
    failures += open_flags_expect("open trunc", ret, 0);
    if (ret == 0) {
        failures += open_flags_expect("stat trunc open",
                                      mr_fs_stat(&file, &stat), 0);
        failures += open_flags_expect("trunc size zero", stat.size, 0);
        failures += open_flags_expect("write after trunc",
                                      mr_fs_write(&file, "xy", 2), 2);
        failures += open_flags_expect("close trunc", mr_fs_close(&file), 0);
    }
    failures += open_flags_expect("stat after trunc write",
                                  fs_test_stat("/seed", &stat), 0);
    failures += open_flags_expect("size after trunc write", stat.size, 2);

    /* O_TRUNC is ignored for a read-only open */
    ret = mr_fs_open(&file, "/seed", MR_O_RDONLY | MR_O_TRUNC);
    failures += open_flags_expect("open trunc readonly", ret, 0);
    if (ret == 0) {
        failures += open_flags_expect("stat trunc readonly",
                                      mr_fs_stat(&file, &stat), 0);
        failures += open_flags_expect("trunc readonly keeps size", stat.size, 2);
        failures += open_flags_expect("close trunc readonly",
                                      mr_fs_close(&file), 0);
    }

    /* Invalid access mode: both WRONLY and RDWR bits set (ACCMODE == 3) */
    failures += open_flags_expect(
        "invalid accmode",
        mr_fs_open(&file, "/seed", MR_O_WRONLY | MR_O_RDWR), -MR_EINVAL);

    /* Zero-length read and write */
    ret = mr_fs_open(&file, "/seed", MR_O_RDWR);
    failures += open_flags_expect("open zero io", ret, 0);
    if (ret == 0) {
        failures += open_flags_expect("read zero", mr_fs_read(&file, buffer, 0),
                                      0);
        failures += open_flags_expect("write zero",
                                      mr_fs_write(&file, buffer, 0), 0);
        failures += open_flags_expect("close zero io", mr_fs_close(&file), 0);
    }

    /* O_DIRECTORY follows a directory symlink unless NOFOLLOW is set */
    failures += open_flags_expect(
        "create directory symlink",
        mr_fs_symlink("flags-dir", "/dir-link"), 0);
    ret = mr_fs_open(&file, "/dir-link", MR_O_RDONLY | MR_O_DIRECTORY);
    failures += open_flags_expect("open directory symlink", ret, 0);
    if (ret == 0) {
        failures += open_flags_expect("close directory symlink",
                                      mr_fs_close(&file), 0);
    }
    failures += open_flags_expect(
        "directory nofollow symlink",
        mr_fs_open(&file, "/dir-link",
                   MR_O_RDONLY | MR_O_DIRECTORY | MR_O_NOFOLLOW),
        -MR_ENOTDIR);

    /* Directory cannot be read as a file */
    ret = mr_fs_open(&file, "/flags-dir", MR_O_RDONLY);
    failures += open_flags_expect("open dir for read", ret, 0);
    if (ret == 0) {
        failures += open_flags_expect(
            "read directory", mr_fs_read(&file, buffer, sizeof(buffer)),
            -MR_EISDIR);
        failures += open_flags_expect("close dir for read",
                                      mr_fs_close(&file), 0);
    }

    /* Readdir rejects a regular file handle opened without O_DIRECTORY */
    ret = mr_fs_open(&other, "/seed", MR_O_RDONLY);
    failures += open_flags_expect("open file readdir", ret, 0);
    if (ret == 0) {
        mr_dirent_t dirent;

        failures += open_flags_expect(
            "readdir on file", mr_fs_readdir(&other, &dirent), -MR_ENOTDIR);
        failures += open_flags_expect("close file readdir",
                                      mr_fs_close(&other), 0);
    }

    failures += open_flags_expect("umount ramfs", mr_fs_umount("/"), 0);
    return failures;
}

static void open_flags(void) {
    MR_TEST_EXPECT_EQ_INT(fs_open_flags_test(), 0);
}
MR_TEST_EXPORT(fs, open_flags, open_flags);
#endif /* defined(MR_USE_TEST) && defined(MR_USE_FS) */
