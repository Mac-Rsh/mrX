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

static int io_errors_expect(const char *name, mr_int64_t actual,
                            mr_int64_t expected) {
    MR_IGNORE(name);

    MR_TEST_EXPECT_EQ_INT(actual, expected);
    return (actual == expected) ? 0 : 1;
}

static int fs_io_errors_test(void) {
    mr_file_t file = MR_FILE_INIT(file);
    mr_file_t dir = MR_FILE_INIT(dir);
    mr_file_t base = MR_FILE_INIT(base);
    char buffer[8];
    mr_stat_t stat;
    int failures = 0;
    mr_err_t ret;

    /* Mount RamFS */
    ret = mr_fs_mount(MR_NULL, "/", "ramfs", 0, MR_NULL);
    failures += io_errors_expect("mount ramfs", ret, 0);
    if (ret != 0) {
        return failures;
    }

    /* Seed nodes */
    ret = mr_fs_open(&file, "/io", MR_O_RDWR | MR_O_CREAT);
    failures += io_errors_expect("create io", ret, 0);
    if (ret == 0) {
        failures += io_errors_expect("write io seed",
                                     mr_fs_write(&file, "abc", 3), 3);
        failures += io_errors_expect("close io", mr_fs_close(&file), 0);
    }
    failures += io_errors_expect("mkdir iodir", mr_fs_mkdir("/iodir"), 0);

    /* Read on write-only handle */
    ret = mr_fs_open(&file, "/io", MR_O_WRONLY);
    failures += io_errors_expect("open writeonly io", ret, 0);
    if (ret == 0) {
        failures += io_errors_expect("read writeonly",
                                     mr_fs_read(&file, buffer, 1), -MR_EBADF);
        failures += io_errors_expect("close writeonly io",
                                     mr_fs_close(&file), 0);
    }

    /* Write on read-only handle */
    ret = mr_fs_open(&file, "/io", MR_O_RDONLY);
    failures += io_errors_expect("open readonly io", ret, 0);
    if (ret == 0) {
        failures += io_errors_expect("write readonly",
                                     mr_fs_write(&file, "x", 1), -MR_EBADF);
        failures += io_errors_expect("close readonly io",
                                     mr_fs_close(&file), 0);
    }

    /* Write / read on a directory handle. A directory is always opened
     * read-only, so write hits the read-only check(-MR_EBADF) before the
     * directory check, while read hits the directory check */
    ret = mr_fs_open(&dir, "/iodir", MR_O_RDONLY);
    failures += io_errors_expect("open iodir", ret, 0);
    if (ret == 0) {
        failures += io_errors_expect("write directory",
                                     mr_fs_write(&dir, "x", 1), -MR_EBADF);
        failures += io_errors_expect("read directory",
                                     mr_fs_read(&dir, buffer, 1), -MR_EISDIR);
        failures += io_errors_expect("close iodir", mr_fs_close(&dir), 0);
    }

    /* Operations on a closed handle */
    failures += io_errors_expect("seek closed", mr_fs_seek(&file, 1, MR_SEEK_SET),
                                 -MR_EBADF);
    failures += io_errors_expect("tell closed", mr_fs_tell(&file), -MR_EBADF);
    failures += io_errors_expect("truncate closed",
                                 mr_fs_truncate(&file, 1), -MR_EBADF);

    /* Truncate on a non-regular node type */
    failures += io_errors_expect("symlink tgt", mr_fs_symlink("io", "/io-link"),
                                 0);
    ret = mr_fs_open(&file, "/io-link", MR_O_RDONLY | MR_O_NOFOLLOW);
    failures += io_errors_expect("open io-link nofollow", ret, 0);
    if (ret == 0) {
        failures += io_errors_expect("truncate link",
                                     mr_fs_truncate(&file, 1), -MR_EINVAL);
        failures += io_errors_expect("readlink size0",
                                     mr_fs_readlink(&file, buffer, 0), 0);
        failures += io_errors_expect("readlink data",
                                     mr_fs_readlink(&file, buffer, 4), 2);
        failures += io_errors_expect("close io-link",
                                     mr_fs_close(&file), 0);
    }

    /* readlink on a regular file handle */
    ret = mr_fs_open(&file, "/io", MR_O_RDONLY);
    failures += io_errors_expect("open io for readlink", ret, 0);
    if (ret == 0) {
        failures += io_errors_expect("readlink on regular",
                                     mr_fs_readlink(&file, buffer, 4),
                                     -MR_EINVAL);
        failures += io_errors_expect("close io for readlink",
                                     mr_fs_close(&file), 0);
    }
    failures += io_errors_expect("readlink closed",
                                 mr_fs_readlink(&file, buffer, 4), -MR_EBADF);

    /* openat relative base validation */
    failures += io_errors_expect(
        "openat null dir", mr_fs_openat(&file, MR_NULL, "rel", MR_O_RDONLY),
        -MR_EBADF);
    ret = mr_fs_open(&base, "/io", MR_O_RDONLY);
    failures += io_errors_expect("openat base file", ret, 0);
    if (ret == 0) {
        failures += io_errors_expect(
            "openat file base",
            mr_fs_openat(&file, &base, "rel", MR_O_RDONLY), -MR_ENOTDIR);
        failures += io_errors_expect("close openat base",
                                     mr_fs_close(&base), 0);
    }
    failures += io_errors_expect(
        "openat closed dir",
        mr_fs_openat(&file, &base, "rel", MR_O_RDONLY), -MR_EBADF);

    /* openat relative success through a directory base */
    ret = mr_fs_open(&dir, "/iodir", MR_O_RDONLY);
    failures += io_errors_expect("open iodir for openat", ret, 0);
    if (ret == 0) {
        failures += io_errors_expect(
            "openat relative",
            mr_fs_openat(&file, &dir, "sub", MR_O_RDWR | MR_O_CREAT), 0);
        if (file.inode) {
            failures += io_errors_expect("close openat sub",
                                         mr_fs_close(&file), 0);
        }
        failures += io_errors_expect("close iodir for openat",
                                     mr_fs_close(&dir), 0);
    }

    /* getcwd buffer too small at root */
    failures += io_errors_expect("chdir root", mr_fs_chdir("/"), 0);
    failures += io_errors_expect("getcwd size1",
                                 mr_fs_getcwd(buffer, 1), -MR_ERANGE);
    failures += io_errors_expect("getcwd root",
                                 mr_fs_getcwd(buffer, 2), 1);
    if (buffer[0] != '/') {
        MR_TEST_EXPECT_EQ_INT(buffer[0], '/');
        failures++;
    }

    /* stat_by_path on a dangling symlink follows and fails */
    failures += io_errors_expect("symlink dangling",
                                 mr_fs_symlink("missing", "/dangling"), 0);
    failures += io_errors_expect("stat dangling follows",
                                 fs_test_stat("/dangling", &stat), -MR_ENOENT);

    failures += io_errors_expect("umount ramfs", mr_fs_umount("/"), 0);
    return failures;
}

static void io_errors(void) {
    MR_TEST_EXPECT_EQ_INT(fs_io_errors_test(), 0);
}
MR_TEST_EXPORT(fs, io_errors, io_errors);
#endif /* defined(MR_USE_TEST) && defined(MR_USE_FS) */
