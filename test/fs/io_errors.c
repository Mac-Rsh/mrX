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

static void io_errors_expect(volatile mr_uint32_t *__test_status,
                             const char *name, mr_int64_t actual,
                             mr_int64_t expected) {
    MR_IGNORE(name);

    MR_TEST_EXPECT_EQ_INT(actual, expected);
}

static void fs_io_errors_test(volatile mr_uint32_t *__test_status) {
    mr_file_t file = MR_FILE_INIT(file);
    mr_file_t dir = MR_FILE_INIT(dir);
    mr_file_t base = MR_FILE_INIT(base);
    char buffer[8];
    mr_stat_t stat;
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
                     mr_fs_seek(&file, 1, MR_SEEK_SET), -MR_EBADF);
    io_errors_expect(__test_status, "tell closed", mr_fs_tell(&file),
                     -MR_EBADF);
    io_errors_expect(__test_status, "truncate closed", mr_fs_truncate(&file, 1),
                     -MR_EBADF);

    /* Truncate on a non-regular node type */
    io_errors_expect(__test_status, "symlink tgt",
                     mr_fs_symlink("io", "/io-link"), 0);
    ret = mr_fs_open(&file, "/io-link", MR_O_RDONLY | MR_O_NOFOLLOW);
    io_errors_expect(__test_status, "open io-link nofollow", ret, 0);
    if (ret == 0) {
        io_errors_expect(__test_status, "truncate link",
                         mr_fs_truncate(&file, 1), -MR_EINVAL);
        io_errors_expect(__test_status, "readlink size0",
                         mr_fs_readlink(&file, buffer, 0), 0);
        io_errors_expect(__test_status, "readlink data",
                         mr_fs_readlink(&file, buffer, 4), 2);
        io_errors_expect(__test_status, "close io-link", mr_fs_close(&file), 0);
    }

    /* readlink on a regular file handle */
    ret = mr_fs_open(&file, "/io", MR_O_RDONLY);
    io_errors_expect(__test_status, "open io for readlink", ret, 0);
    if (ret == 0) {
        io_errors_expect(__test_status, "readlink on regular",
                         mr_fs_readlink(&file, buffer, 4), -MR_EINVAL);
        io_errors_expect(__test_status, "close io for readlink",
                         mr_fs_close(&file), 0);
    }
    io_errors_expect(__test_status, "readlink closed",
                     mr_fs_readlink(&file, buffer, 4), -MR_EBADF);

    /* openat relative base validation */
    io_errors_expect(__test_status, "openat null dir",
                     mr_fs_openat(&file, MR_NULL, "rel", MR_O_RDONLY),
                     -MR_EBADF);
    ret = mr_fs_open(&base, "/io", MR_O_RDONLY);
    io_errors_expect(__test_status, "openat base file", ret, 0);
    if (ret == 0) {
        io_errors_expect(__test_status, "openat file base",
                         mr_fs_openat(&file, &base, "rel", MR_O_RDONLY),
                         -MR_ENOTDIR);
        io_errors_expect(__test_status, "close openat base", mr_fs_close(&base),
                         0);
    }
    io_errors_expect(__test_status, "openat closed dir",
                     mr_fs_openat(&file, &base, "rel", MR_O_RDONLY), -MR_EBADF);

    /* openat relative success through a directory base */
    ret = mr_fs_open(&dir, "/iodir", MR_O_RDONLY);
    io_errors_expect(__test_status, "open iodir for openat", ret, 0);
    if (ret == 0) {
        io_errors_expect(
            __test_status, "openat relative",
            mr_fs_openat(&file, &dir, "sub", MR_O_RDWR | MR_O_CREAT), 0);
        if (file.inode) {
            io_errors_expect(__test_status, "close openat sub",
                             mr_fs_close(&file), 0);
        }
        io_errors_expect(__test_status, "close iodir for openat",
                         mr_fs_close(&dir), 0);
    }

    /* getcwd buffer too small at root */
    io_errors_expect(__test_status, "chdir root", mr_fs_chdir("/"), 0);
    io_errors_expect(__test_status, "getcwd size1", mr_fs_getcwd(buffer, 1),
                     -MR_ERANGE);
    io_errors_expect(__test_status, "getcwd root", mr_fs_getcwd(buffer, 2), 1);
    if (buffer[0] != '/') {
        MR_TEST_EXPECT_EQ_INT(buffer[0], '/');
    }

    /* stat_by_path on a dangling symlink follows and fails */
    io_errors_expect(__test_status, "symlink dangling",
                     mr_fs_symlink("missing", "/dangling"), 0);
    io_errors_expect(__test_status, "stat dangling follows",
                     fs_test_stat("/dangling", &stat), -MR_ENOENT);

    io_errors_expect(__test_status, "umount ramfs", mr_fs_umount("/"), 0);
    return;
}

MR_TEST(fs, io_errors) {
    fs_io_errors_test(__test_status);
}

#endif /* defined(MR_USE_TEST) && defined(MR_USE_FS) */
