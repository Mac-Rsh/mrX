/**
 * @copyright (c) 2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2026-07-13        MacRsh        First version
 */

#include <mrX/test/mr_test.h>
#if defined(MR_USE_TEST) && defined(MR_USE_FS)
#include <mrX.h>
#include "fs_test.h"

static int fs_file_expect(const char *name, mr_int64_t actual,
                          mr_int64_t expected) {
    MR_IGNORE(name);

    MR_TEST_EXPECT_EQ_INT(actual, expected);
    return (actual == expected) ? 0 : 1;
}

static int fs_file_data_expect(const char *name, const char *expected,
                               mr_size_t size) {
    mr_file_t file = MR_FILE_INIT(file);
    char buffer[16];
    mr_ssize_t ret;
    int failures = 0;

    /* Read file data */
    ret = mr_fs_open(&file, "/file", MR_O_RDONLY);
    failures += fs_file_expect(name, ret, 0);
    if (ret != 0) {
        return failures;
    }
    ret = mr_fs_read(&file, buffer, sizeof(buffer));
    failures += fs_file_expect(name, ret, (mr_ssize_t)size);
    if ((ret == (mr_ssize_t)size) && (mr_memcmp(buffer, expected, size) != 0)) {
        MR_TEST_EXPECT_EQ_INT(mr_memcmp(buffer, expected, size), 0);
        failures++;
    }
    failures
        += fs_file_expect(name, mr_fs_read(&file, buffer, sizeof(buffer)), 0);
    failures += fs_file_expect(name, mr_fs_close(&file), 0);
    return failures;
}

static int fs_file_test(void) {
    mr_file_t append_file = MR_FILE_INIT(append_file);
    mr_file_t other_file = {0};
    mr_file_t initialized_file;
    mr_stat_t stat;
    char buffer[4];
    mr_ssize_t io_ret;
    int failures = 0;
    mr_err_t ret;

    /* Mount RAMFS */
    ret = mr_fs_mount(MR_NULL, "/", "ramfs", 0, MR_NULL);
    failures += fs_file_expect("file mount ramfs", ret, 0);
    if (ret != 0) {
        return failures;
    }

    /* Initialize a file handle with both supported interfaces */
    mr_file_init(&initialized_file);
    failures += fs_file_expect("close function-initialized file",
                               mr_fs_close(&initialized_file), -MR_EBADF);
    failures += fs_file_expect("close initialized file",
                               mr_fs_close(&append_file), -MR_EBADF);

    /* Seed file */
    ret = mr_fs_open(&other_file, "/file", MR_O_RDWR | MR_O_CREAT);
    failures += fs_file_expect("open seed file", ret, 0);
    if (ret == 0) {
        failures += fs_file_expect(
            "reject already open file",
            mr_fs_open(&other_file, "/file", MR_O_RDONLY), -MR_EBUSY);
        failures += fs_file_expect("write seed file",
                                   mr_fs_write(&other_file, "ab", 2), 2);
        failures
            += fs_file_expect("close seed file", mr_fs_close(&other_file), 0);
        failures += fs_file_expect("double close seed file",
                                   mr_fs_close(&other_file), -MR_EBADF);
        failures
            += fs_file_expect("reopen seed file",
                              mr_fs_open(&other_file, "/file", MR_O_RDONLY), 0);
        failures += fs_file_expect("close reopened seed file",
                                   mr_fs_close(&other_file), 0);
        mr_file_init(&other_file);
        failures += fs_file_expect("close reinitialized file",
                                   mr_fs_close(&other_file), -MR_EBADF);
    }

    /* Append after seeking away from EOF */
    ret = mr_fs_open(&append_file, "/file", MR_O_RDWR | MR_O_APPEND);
    failures += fs_file_expect("open append file", ret, 0);
    if (ret != 0) {
        goto _exit;
    }
    failures += fs_file_expect("seek append file",
                               mr_fs_seek(&append_file, 0, MR_SEEK_SET), 0);
    failures += fs_file_expect("write append file",
                               mr_fs_write(&append_file, "c", 1), 1);
    failures += fs_file_data_expect("append after seek", "abc", 3);
    failures += fs_file_expect("seek before zero append",
                               mr_fs_seek(&append_file, 0, MR_SEEK_SET), 0);
    failures += fs_file_expect("zero-length append",
                               mr_fs_write(&append_file, "z", 0), 0);
    failures += fs_file_expect("position after zero append",
                               mr_fs_tell(&append_file), 0);

    /* Append after another handle truncates the file */
    ret = mr_fs_open(&other_file, "/file", MR_O_WRONLY | MR_O_TRUNC);
    failures += fs_file_expect("open truncate file", ret, 0);
    if (ret == 0) {
        failures += fs_file_expect("close truncate file",
                                   mr_fs_close(&other_file), 0);
    }
    failures += fs_file_expect("seek past truncated file",
                               mr_fs_seek(&append_file, 7, MR_SEEK_SET), 7);
    failures += fs_file_expect("append after truncate",
                               mr_fs_write(&append_file, "d", 1), 1);
    failures += fs_file_data_expect("append truncated file", "d", 1);

    /* Append after another handle extends the file */
    ret = mr_fs_open(&other_file, "/file", MR_O_WRONLY);
    failures += fs_file_expect("open extending file", ret, 0);
    if (ret == 0) {
        failures += fs_file_expect("seek extending file",
                                   mr_fs_seek(&other_file, 0, MR_SEEK_END), 1);
        failures += fs_file_expect("extend file",
                                   mr_fs_write(&other_file, "e", 1), 1);
        failures += fs_file_expect("close extending file",
                                   mr_fs_close(&other_file), 0);
    }
    failures += fs_file_expect("reset append position",
                               mr_fs_seek(&append_file, 0, MR_SEEK_SET), 0);
    failures += fs_file_expect("append after extend",
                               mr_fs_write(&append_file, "f", 1), 1);
    failures += fs_file_data_expect("append extended file", "def", 3);

    /* Preserve normal positioned writes */
    ret = mr_fs_open(&other_file, "/file", MR_O_WRONLY);
    failures += fs_file_expect("open positioned file", ret, 0);
    if (ret == 0) {
        failures += fs_file_expect("seek positioned file",
                                   mr_fs_seek(&other_file, 1, MR_SEEK_SET), 1);
        failures += fs_file_expect("write positioned file",
                                   mr_fs_write(&other_file, "X", 1), 1);
        failures += fs_file_expect("close positioned file",
                                   mr_fs_close(&other_file), 0);
    }
    failures += fs_file_data_expect("positioned write", "dXf", 3);

    /* Reject seek and write overflow without changing state */
    ret = mr_fs_open(&other_file, "/file", MR_O_WRONLY);
    failures += fs_file_expect("open range file", ret, 0);
    if (ret == 0) {
        failures += fs_file_expect(
            "seek maximum position",
            mr_fs_seek(&other_file, MR_INT64_MAX, MR_SEEK_SET), MR_INT64_MAX);
        failures += fs_file_expect("reject seek overflow",
                                   mr_fs_seek(&other_file, 1, MR_SEEK_CUR),
                                   -MR_ERANGE);
        failures += fs_file_expect("position after seek overflow",
                                   mr_fs_tell(&other_file), MR_INT64_MAX);
        failures += fs_file_expect("seek down from maximum",
                                   mr_fs_seek(&other_file, -1, MR_SEEK_CUR),
                                   MR_INT64_MAX - 1);
        failures += fs_file_expect(
            "restore maximum position",
            mr_fs_seek(&other_file, MR_INT64_MAX, MR_SEEK_SET), MR_INT64_MAX);
        failures
            += fs_file_expect("reject write overflow",
                              mr_fs_write(&other_file, "Y", 1), -MR_ERANGE);
        failures += fs_file_expect("position after write overflow",
                                   mr_fs_tell(&other_file), MR_INT64_MAX);

        /* A representable negative result retains the existing clamp rule */
        failures += fs_file_expect("reset range position",
                                   mr_fs_seek(&other_file, 0, MR_SEEK_SET), 0);
        failures += fs_file_expect(
            "clamp minimum offset",
            mr_fs_seek(&other_file, -MR_INT64_MAX - 1, MR_SEEK_CUR), 0);

        /* Exercise the lower-bound guard independently of normal invariants */
        other_file.pos = -MR_INT64_MAX - 1;
        failures += fs_file_expect("reject seek underflow",
                                   mr_fs_seek(&other_file, -1, MR_SEEK_CUR),
                                   -MR_ERANGE);
        failures += fs_file_expect("position after seek underflow",
                                   other_file.pos, -MR_INT64_MAX - 1);
        failures += fs_file_expect("seek up from minimum",
                                   mr_fs_seek(&other_file, 1, MR_SEEK_CUR), 0);
        other_file.pos = 0;
        failures
            += fs_file_expect("close range file", mr_fs_close(&other_file), 0);
    }
    failures += fs_file_data_expect("data after range errors", "dXf", 3);

    /* Close append file */
    failures
        += fs_file_expect("close append file", mr_fs_close(&append_file), 0);

    /* Reject closed file operations */
    failures += fs_file_expect("stat closed file",
                               mr_fs_stat(&other_file, &stat), -MR_EBADF);
    failures += fs_file_expect("unlink closed file", mr_fs_unlink(&other_file),
                               -MR_EBADF);
    failures += fs_file_expect("rename closed file",
                               mr_fs_rename(&other_file, "/closed"), -MR_EBADF);

    /* Keep an open file across rename and unlink */
    ret = mr_fs_open(&other_file, "/file", MR_O_RDWR);
    failures += fs_file_expect("open namespace file", ret, 0);
    if (ret == 0) {
        ret = mr_fs_stat(&other_file, &stat);
        failures += fs_file_expect("stat namespace file", ret, 0);
        if (ret == 0) {
            failures += fs_file_expect("namespace file type", stat.type,
                                       MR_INODE_REG);
            failures += fs_file_expect("namespace file links", stat.nlink, 1);
            failures += fs_file_expect("namespace file size", stat.size, 3);
        }

        failures
            += fs_file_expect("rename open file by path",
                              fs_test_rename("/file", "/file-path-renamed"), 0);
        failures += fs_file_expect("stat after path rename",
                                   mr_fs_stat(&other_file, &stat), 0);
        failures += fs_file_expect(
            "rename open file by handle",
            mr_fs_rename(&other_file, "/file-handle-renamed"), 0);
        failures += fs_file_expect("old handle path absent",
                                   fs_test_stat("/file-path-renamed", &stat),
                                   -MR_ENOENT);
        failures
            += fs_file_expect("new handle path present",
                              fs_test_stat("/file-handle-renamed", &stat), 0);

        failures
            += fs_file_expect("unlink open file", mr_fs_unlink(&other_file), 0);
        failures += fs_file_expect("unlinked path absent",
                                   fs_test_stat("/file-handle-renamed", &stat),
                                   -MR_ENOENT);
        ret = mr_fs_stat(&other_file, &stat);
        failures += fs_file_expect("stat unlinked file", ret, 0);
        if (ret == 0) {
            failures += fs_file_expect("unlinked file links", stat.nlink, 0);
            failures += fs_file_expect("unlinked file size", stat.size, 3);
        }

        failures += fs_file_expect("seek unlinked file",
                                   mr_fs_seek(&other_file, 0, MR_SEEK_SET), 0);
        io_ret = mr_fs_read(&other_file, buffer, sizeof(buffer));
        failures += fs_file_expect("read unlinked file", io_ret, 3);
        if ((io_ret == 3) && (mr_memcmp(buffer, "dXf", 3) != 0)) {
            MR_TEST_EXPECT_EQ_INT(mr_memcmp(buffer, "dXf", 3), 0);
            failures++;
        }
        failures += fs_file_expect("seek unlinked file end",
                                   mr_fs_seek(&other_file, 0, MR_SEEK_END), 3);
        failures += fs_file_expect("write unlinked file",
                                   mr_fs_write(&other_file, "!", 1), 1);
        ret = mr_fs_stat(&other_file, &stat);
        failures += fs_file_expect("stat extended unlinked file", ret, 0);
        if (ret == 0) {
            failures
                += fs_file_expect("extended unlinked file size", stat.size, 4);
        }
        failures += fs_file_expect("repeat unlink open file",
                                   mr_fs_unlink(&other_file), -MR_ENOENT);
        failures += fs_file_expect("rename unlinked file",
                                   mr_fs_rename(&other_file, "/file-restored"),
                                   -MR_ENOENT);
        failures += fs_file_expect("close unlinked file",
                                   mr_fs_close(&other_file), 0);
    }

    /* Keep an open file after path unlink */
    ret = mr_fs_open(&other_file, "/path-unlink", MR_O_RDWR | MR_O_CREAT);
    failures += fs_file_expect("open path unlink file", ret, 0);
    if (ret == 0) {
        failures += fs_file_expect("unlink open file by path",
                                   fs_test_unlink("/path-unlink"), 0);
        failures
            += fs_file_expect("path unlinked file absent",
                              fs_test_stat("/path-unlink", &stat), -MR_ENOENT);
        ret = mr_fs_stat(&other_file, &stat);
        failures += fs_file_expect("stat path unlinked file", ret, 0);
        if (ret == 0) {
            failures
                += fs_file_expect("path unlinked file links", stat.nlink, 0);
        }
        failures += fs_file_expect("close path unlinked file",
                                   mr_fs_close(&other_file), 0);
    }

    /* Exercise direct path metadata operations without opening a handle */
    ret = mr_fs_open(&other_file, "/path-api", MR_O_RDWR | MR_O_CREAT);
    failures += fs_file_expect("create path api file", ret, 0);
    if (ret == 0) {
        failures += fs_file_expect("close path api file",
                                   mr_fs_close(&other_file), 0);
    }
    failures += fs_file_expect("stat path api file",
                               mr_fs_stat_by_path("/path-api", &stat), 0);
    failures += fs_file_expect(
        "rename path api file",
        mr_fs_rename_by_path("/path-api", "/path-api-renamed"), 0);
    failures += fs_file_expect("old path api name absent",
                               mr_fs_stat_by_path("/path-api", &stat), -MR_ENOENT);
    failures += fs_file_expect("renamed path api name present",
                               mr_fs_stat_by_path("/path-api-renamed", &stat), 0);
    failures += fs_file_expect("unlink path api file",
                               mr_fs_unlink_by_path("/path-api-renamed"), 0);
    failures += fs_file_expect("path api file absent",
                               mr_fs_stat_by_path("/path-api-renamed", &stat),
                               -MR_ENOENT);

_exit:
    /* Unmount RAMFS */
    failures += fs_file_expect("file umount ramfs", mr_fs_umount("/"), 0);
    return failures;
}

static void file(void) {
    MR_TEST_EXPECT_EQ_INT(fs_file_test(), 0);
}
MR_TEST_EXPORT(fs, file, file);
#endif /* defined(MR_USE_TEST) && defined(MR_USE_FS) */
