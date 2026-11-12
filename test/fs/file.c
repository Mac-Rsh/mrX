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
#include <mrX/test/fs/fs_test.h>

static void fs_file_expect(volatile mr_uint32_t *__test_status,
                           const char *name, mr_int64_t actual,
                           mr_int64_t expected) {
    MR_IGNORE(name);

    MR_TEST_EXPECT_EQ_INT(actual, expected);
}

#if defined(MR_USE_FS_RAMFS)
static void fs_file_data_expect(volatile mr_uint32_t *__test_status,
                                const char *name, const char *expected,
                                mr_size_t size) {
    mr_file_t file = MR_FILE_INIT(file);
    char buffer[16];
    mr_ssize_t ret;

    /* Read file data */
    ret = mr_fs_open(&file, "/file", MR_O_RDONLY);
    fs_file_expect(__test_status, name, ret, 0);
    if (ret != 0) {
        return;
    }
    ret = mr_fs_read(&file, buffer, sizeof(buffer));
    fs_file_expect(__test_status, name, ret, (mr_ssize_t)size);
    if ((ret == (mr_ssize_t)size) && (mr_memcmp(buffer, expected, size) != 0)) {
        MR_TEST_EXPECT_EQ_INT(mr_memcmp(buffer, expected, size), 0);
    }
    fs_file_expect(__test_status, name,
                   mr_fs_read(&file, buffer, sizeof(buffer)), 0);
    fs_file_expect(__test_status, name, mr_fs_close(&file), 0);
    return;
}

MR_TEST(fs, file) {
    mr_file_t append_file = MR_FILE_INIT(append_file);
    mr_file_t other_file = {0};
    mr_file_t initialized_file;
    mr_stat_t stat;
    char buffer[4];
    mr_ssize_t io_ret;
    mr_err_t ret;

    /* Mount RAMFS */
    ret = fs_test_mount_root("ramfs", 0, MR_NULL);
    fs_file_expect(__test_status, "file mount ramfs", ret, 0);
    if (ret != 0) {
        return;
    }

    /* Initialize a file handle with both supported interfaces */
    mr_file_init(&initialized_file);
    fs_file_expect(__test_status, "close function-initialized file",
                   mr_fs_close(&initialized_file), -MR_EBADF);
    fs_file_expect(__test_status, "close initialized file",
                   mr_fs_close(&append_file), -MR_EBADF);

    /* Seed file */
    ret = mr_fs_open(&other_file, "/file", MR_O_RDWR | MR_O_CREAT);
    fs_file_expect(__test_status, "open seed file", ret, 0);
    if (ret == 0) {
        fs_file_expect(__test_status, "reject already open file",
                       mr_fs_open(&other_file, "/file", MR_O_RDONLY),
                       -MR_EBUSY);
        fs_file_expect(__test_status, "write seed file",
                       mr_fs_write(&other_file, "ab", 2), 2);
        fs_file_expect(__test_status, "close seed file",
                       mr_fs_close(&other_file), 0);
        fs_file_expect(__test_status, "double close seed file",
                       mr_fs_close(&other_file), -MR_EBADF);
        fs_file_expect(__test_status, "reopen seed file",
                       mr_fs_open(&other_file, "/file", MR_O_RDONLY), 0);
        fs_file_expect(__test_status, "close reopened seed file",
                       mr_fs_close(&other_file), 0);
        mr_file_init(&other_file);
        fs_file_expect(__test_status, "close reinitialized file",
                       mr_fs_close(&other_file), -MR_EBADF);
    }

    /* Append after seeking away from EOF */
    ret = mr_fs_open(&append_file, "/file", MR_O_RDWR | MR_O_APPEND);
    fs_file_expect(__test_status, "open append file", ret, 0);
    if (ret != 0) {
        goto _exit;
    }
    fs_file_expect(__test_status, "seek append file",
                   mr_fs_lseek(&append_file, 0, MR_SEEK_SET), 0);
    fs_file_expect(__test_status, "write append file",
                   mr_fs_write(&append_file, "c", 1), 1);
    fs_file_data_expect(__test_status, "append after seek", "abc", 3);
    fs_file_expect(__test_status, "seek before zero append",
                   mr_fs_lseek(&append_file, 0, MR_SEEK_SET), 0);
    fs_file_expect(__test_status, "zero-length append",
                   mr_fs_write(&append_file, "z", 0), 0);
    fs_file_expect(__test_status, "position after zero append",
                   mr_fs_lseek(&append_file, 0, MR_SEEK_CUR), 0);

    /* Append after another handle truncates the file */
    ret = mr_fs_open(&other_file, "/file", MR_O_WRONLY | MR_O_TRUNC);
    fs_file_expect(__test_status, "open truncate file", ret, 0);
    if (ret == 0) {
        fs_file_expect(__test_status, "close truncate file",
                       mr_fs_close(&other_file), 0);
    }
    fs_file_expect(__test_status, "seek past truncated file",
                   mr_fs_lseek(&append_file, 7, MR_SEEK_SET), 7);
    fs_file_expect(__test_status, "append after truncate",
                   mr_fs_write(&append_file, "d", 1), 1);
    fs_file_data_expect(__test_status, "append truncated file", "d", 1);

    /* Append after another handle extends the file */
    ret = mr_fs_open(&other_file, "/file", MR_O_WRONLY);
    fs_file_expect(__test_status, "open extending file", ret, 0);
    if (ret == 0) {
        fs_file_expect(__test_status, "seek extending file",
                       mr_fs_lseek(&other_file, 0, MR_SEEK_END), 1);
        fs_file_expect(__test_status, "extend file",
                       mr_fs_write(&other_file, "e", 1), 1);
        fs_file_expect(__test_status, "close extending file",
                       mr_fs_close(&other_file), 0);
    }
    fs_file_expect(__test_status, "reset append position",
                   mr_fs_lseek(&append_file, 0, MR_SEEK_SET), 0);
    fs_file_expect(__test_status, "append after extend",
                   mr_fs_write(&append_file, "f", 1), 1);
    fs_file_data_expect(__test_status, "append extended file", "def", 3);

    /* Preserve normal positioned writes */
    ret = mr_fs_open(&other_file, "/file", MR_O_WRONLY);
    fs_file_expect(__test_status, "open positioned file", ret, 0);
    if (ret == 0) {
        fs_file_expect(__test_status, "seek positioned file",
                       mr_fs_lseek(&other_file, 1, MR_SEEK_SET), 1);
        fs_file_expect(__test_status, "write positioned file",
                       mr_fs_write(&other_file, "X", 1), 1);
        fs_file_expect(__test_status, "close positioned file",
                       mr_fs_close(&other_file), 0);
    }
    fs_file_data_expect(__test_status, "positioned write", "dXf", 3);

    /* Reject seek and write overflow without changing state */
    ret = mr_fs_open(&other_file, "/file", MR_O_WRONLY);
    fs_file_expect(__test_status, "open range file", ret, 0);
    if (ret == 0) {
        fs_file_expect(__test_status, "seek maximum position",
                       mr_fs_lseek(&other_file, MR_INT64_MAX, MR_SEEK_SET),
                       MR_INT64_MAX);
        fs_file_expect(__test_status, "reject seek overflow",
                       mr_fs_lseek(&other_file, 1, MR_SEEK_CUR), -MR_ERANGE);
        fs_file_expect(__test_status, "position after seek overflow",
                       mr_fs_lseek(&other_file, 0, MR_SEEK_CUR), MR_INT64_MAX);
        fs_file_expect(__test_status, "seek down from maximum",
                       mr_fs_lseek(&other_file, -1, MR_SEEK_CUR),
                       MR_INT64_MAX - 1);
        fs_file_expect(__test_status, "restore maximum position",
                       mr_fs_lseek(&other_file, MR_INT64_MAX, MR_SEEK_SET),
                       MR_INT64_MAX);
        fs_file_expect(__test_status, "reject write overflow",
                       mr_fs_write(&other_file, "Y", 1), -MR_ERANGE);
        fs_file_expect(__test_status, "position after write overflow",
                       mr_fs_lseek(&other_file, 0, MR_SEEK_CUR), MR_INT64_MAX);

        /* A representable negative result is rejected */
        fs_file_expect(__test_status, "reset range position",
                       mr_fs_lseek(&other_file, 0, MR_SEEK_SET), 0);
        fs_file_expect(__test_status, "reject negative position",
                       mr_fs_lseek(&other_file, -MR_INT64_MAX - 1, MR_SEEK_CUR),
                       -MR_EINVAL);
        fs_file_expect(__test_status, "position after negative seek",
                       mr_fs_lseek(&other_file, 0, MR_SEEK_CUR), 0);
        fs_file_expect(__test_status, "close range file",
                       mr_fs_close(&other_file), 0);
    }
    fs_file_data_expect(__test_status, "data after range errors", "dXf", 3);

    /* Close append file */
    fs_file_expect(__test_status, "close append file",
                   mr_fs_close(&append_file), 0);

    /* Reject closed file operations */
    fs_file_expect(__test_status, "stat closed file",
                   mr_fs_fstat(&other_file, &stat), -MR_EBADF);
    fs_file_expect(__test_status, "unlink missing path",
                   mr_fs_unlink("/closed"), -MR_ENOENT);
    fs_file_expect(__test_status, "rename missing path",
                   mr_fs_rename("/closed", "/renamed"), -MR_ENOENT);

    /* Keep an open file across rename and unlink */
    ret = mr_fs_open(&other_file, "/file", MR_O_RDWR);
    fs_file_expect(__test_status, "open namespace file", ret, 0);
    if (ret == 0) {
        ret = mr_fs_fstat(&other_file, &stat);
        fs_file_expect(__test_status, "stat namespace file", ret, 0);
        if (ret == 0) {
            fs_file_expect(__test_status, "namespace file type", stat.type,
                           MR_INODE_REG);
            fs_file_expect(__test_status, "namespace file size", stat.size, 3);
        }

        fs_file_expect(__test_status, "rename open file by path",
                       mr_fs_rename("/file", "/file-path-renamed"), 0);
        fs_file_expect(__test_status, "stat after path rename",
                       mr_fs_fstat(&other_file, &stat), 0);
        fs_file_expect(
            __test_status, "rename open file again",
            mr_fs_rename("/file-path-renamed", "/file-handle-renamed"), 0);
        fs_file_expect(__test_status, "old path absent",
                       fs_test_stat("/file-path-renamed", &stat), -MR_ENOENT);
        fs_file_expect(__test_status, "new path present",
                       fs_test_stat("/file-handle-renamed", &stat), 0);

        fs_file_expect(__test_status, "unlink open file",
                       mr_fs_unlink("/file-handle-renamed"), 0);
        fs_file_expect(__test_status, "unlinked path absent",
                       fs_test_stat("/file-handle-renamed", &stat), -MR_ENOENT);
        ret = mr_fs_fstat(&other_file, &stat);
        fs_file_expect(__test_status, "stat unlinked file", ret, 0);
        if (ret == 0) {
            fs_file_expect(__test_status, "unlinked file size", stat.size, 3);
        }

        fs_file_expect(__test_status, "seek unlinked file",
                       mr_fs_lseek(&other_file, 0, MR_SEEK_SET), 0);
        io_ret = mr_fs_read(&other_file, buffer, sizeof(buffer));
        fs_file_expect(__test_status, "read unlinked file", io_ret, 3);
        if ((io_ret == 3) && (mr_memcmp(buffer, "dXf", 3) != 0)) {
            MR_TEST_EXPECT_EQ_INT(mr_memcmp(buffer, "dXf", 3), 0);
        }
        fs_file_expect(__test_status, "seek unlinked file end",
                       mr_fs_lseek(&other_file, 0, MR_SEEK_END), 3);
        fs_file_expect(__test_status, "write unlinked file",
                       mr_fs_write(&other_file, "!", 1), 1);
        ret = mr_fs_fstat(&other_file, &stat);
        fs_file_expect(__test_status, "stat extended unlinked file", ret, 0);
        if (ret == 0) {
            fs_file_expect(__test_status, "extended unlinked file size",
                           stat.size, 4);
        }
        fs_file_expect(__test_status, "repeat unlink open file",
                       mr_fs_unlink("/file-handle-renamed"), -MR_ENOENT);
        fs_file_expect(__test_status, "rename unlinked file",
                       mr_fs_rename("/file-handle-renamed", "/file-restored"),
                       -MR_ENOENT);
        fs_file_expect(__test_status, "close unlinked file",
                       mr_fs_close(&other_file), 0);
    }

    /* Keep an open file after path unlink */
    ret = mr_fs_open(&other_file, "/path-unlink", MR_O_RDWR | MR_O_CREAT);
    fs_file_expect(__test_status, "open path unlink file", ret, 0);
    if (ret == 0) {
        fs_file_expect(__test_status, "unlink open file by path",
                       mr_fs_unlink("/path-unlink"), 0);
        fs_file_expect(__test_status, "path unlinked file absent",
                       fs_test_stat("/path-unlink", &stat), -MR_ENOENT);
        ret = mr_fs_fstat(&other_file, &stat);
        fs_file_expect(__test_status, "stat path unlinked file", ret, 0);
        fs_file_expect(__test_status, "close path unlinked file",
                       mr_fs_close(&other_file), 0);
    }

    /* Exercise direct path metadata operations without opening a handle */
    ret = mr_fs_open(&other_file, "/path-api", MR_O_RDWR | MR_O_CREAT);
    fs_file_expect(__test_status, "create path api file", ret, 0);
    if (ret == 0) {
        fs_file_expect(__test_status, "close path api file",
                       mr_fs_close(&other_file), 0);
    }
    fs_file_expect(__test_status, "stat path api file",
                   fs_test_stat("/path-api", &stat), 0);
    fs_file_expect(__test_status, "rename path api file",
                   mr_fs_rename("/path-api", "/path-api-renamed"), 0);
    fs_file_expect(__test_status, "old path api name absent",
                   fs_test_stat("/path-api", &stat), -MR_ENOENT);
    fs_file_expect(__test_status, "renamed path api name present",
                   fs_test_stat("/path-api-renamed", &stat), 0);
    fs_file_expect(__test_status, "unlink path api file",
                   mr_fs_unlink("/path-api-renamed"), 0);
    fs_file_expect(__test_status, "path api file absent",
                   fs_test_stat("/path-api-renamed", &stat), -MR_ENOENT);

_exit:
    /* Unmount RAMFS */
    fs_file_expect(__test_status, "file umount ramfs", mr_fs_umount("/"), 0);
    return;
}

/* Test: file handle helper interfaces without a mounted file-system */
#endif /* defined(MR_USE_FS_RAMFS) */

MR_TEST(fs, file_helpers) {
    mr_file_t file;
    mr_inode_t inode;
    mr_err_t ret;

    /* A fresh handle reports closed and reopenable */
    mr_file_init(&file);
    fs_file_expect(__test_status, "fresh file closed", mr_file_is_open(&file),
                   MR_FALSE);
    fs_file_expect(__test_status, "close fresh file", mr_fs_close(&file),
                   -MR_EBADF);

    /* Binding an inode makes the handle report open */
    ret = mr_inode_init(&inode, MR_INODE_REG, MR_NULL, MR_NULL, MR_NULL);
    fs_file_expect(__test_status, "inode init", ret, 0);
    inode.size = 7;
    file.inode = &inode;
    fs_file_expect(__test_status, "bound file open", mr_file_is_open(&file),
                   MR_TRUE);

    /* The append selector only moves to EOF for append handles */
    file.pos = 1;
    file.flags = 0;
    mr_file_apply_append(&file);
    fs_file_expect(__test_status, "non-append position kept", file.pos, 1);
    file.flags = MR_O_APPEND;
    mr_file_apply_append(&file);
    fs_file_expect(__test_status, "append position at EOF", file.pos, 7);

    /* Reinit clears the binding and the position */
    mr_file_init(&file);
    fs_file_expect(__test_status, "reinit file closed", mr_file_is_open(&file),
                   MR_FALSE);
    fs_file_expect(__test_status, "reinit position", file.pos, 0);
    return;
}

#endif /* defined(MR_USE_TEST) && defined(MR_USE_FS) */
