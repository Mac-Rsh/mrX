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

static void truncate_expect(volatile mr_uint32_t *__test_status,
                            const char *name, mr_int64_t actual,
                            mr_int64_t expected) {
    MR_IGNORE(name);

    MR_TEST_EXPECT_EQ_INT(actual, expected);
}

static void fs_truncate_test(volatile mr_uint32_t *__test_status) {
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
                    mr_fs_truncate(&file, 10), 0);
    truncate_expect(__test_status, "stat same size", mr_fs_stat(&file, &stat),
                    0);
    truncate_expect(__test_status, "same size value", stat.size, 10);

    /* Shrink */
    truncate_expect(__test_status, "truncate shrink", mr_fs_truncate(&file, 4),
                    0);
    truncate_expect(__test_status, "stat shrink", mr_fs_stat(&file, &stat), 0);
    truncate_expect(__test_status, "shrink size", stat.size, 4);
    truncate_expect(__test_status, "seek shrink head",
                    mr_fs_seek(&file, 0, MR_SEEK_SET), 0);
    io_ret = mr_fs_read(&file, buffer, sizeof(buffer));
    truncate_expect(__test_status, "read shrink", io_ret, 4);
    if ((io_ret == 4) && (mr_memcmp(buffer, "0123", 4) != 0)) {
        MR_TEST_EXPECT_EQ_INT(mr_memcmp(buffer, "0123", 4), 0);
    }

    /* Grow with zero fill */
    truncate_expect(__test_status, "truncate grow", mr_fs_truncate(&file, 8),
                    0);
    truncate_expect(__test_status, "stat grow", mr_fs_stat(&file, &stat), 0);
    truncate_expect(__test_status, "grow size", stat.size, 8);
    truncate_expect(__test_status, "seek grow head",
                    mr_fs_seek(&file, 0, MR_SEEK_SET), 0);
    io_ret = mr_fs_read(&file, buffer, sizeof(buffer));
    truncate_expect(__test_status, "read grow", io_ret, 8);
    if ((io_ret == 8) && (mr_memcmp(buffer, "0123\0\0\0\0", 8) != 0)) {
        MR_TEST_EXPECT_EQ_INT(mr_memcmp(buffer, "0123\0\0\0\0", 8), 0);
    }

    /* Truncate to zero */
    truncate_expect(__test_status, "truncate zero", mr_fs_truncate(&file, 0),
                    0);
    truncate_expect(__test_status, "stat zero", mr_fs_stat(&file, &stat), 0);
    truncate_expect(__test_status, "zero size", stat.size, 0);
    truncate_expect(__test_status, "seek zero head",
                    mr_fs_seek(&file, 0, MR_SEEK_SET), 0);
    truncate_expect(__test_status, "read empty",
                    mr_fs_read(&file, buffer, sizeof(buffer)), 0);

    /* Hole write beyond EOF zero-fills the gap */
    truncate_expect(__test_status, "seek hole",
                    mr_fs_seek(&file, 4, MR_SEEK_SET), 4);
    truncate_expect(__test_status, "write hole", mr_fs_write(&file, "AB", 2),
                    2);
    truncate_expect(__test_status, "stat hole", mr_fs_stat(&file, &stat), 0);
    truncate_expect(__test_status, "hole size", stat.size, 6);
    truncate_expect(__test_status, "seek hole head",
                    mr_fs_seek(&file, 0, MR_SEEK_SET), 0);
    io_ret = mr_fs_read(&file, buffer, sizeof(buffer));
    truncate_expect(__test_status, "read hole", io_ret, 6);
    if ((io_ret == 6) && (mr_memcmp(buffer, "\0\0\0\0AB", 6) != 0)) {
        MR_TEST_EXPECT_EQ_INT(mr_memcmp(buffer, "\0\0\0\0AB", 6), 0);
    }

    /* Negative length is invalid */
    truncate_expect(__test_status, "truncate negative",
                    mr_fs_truncate(&file, -1), -MR_EINVAL);

    /* Truncate does not change the file position */
    truncate_expect(__test_status, "seek mid",
                    mr_fs_seek(&file, 2, MR_SEEK_SET), 2);
    truncate_expect(__test_status, "truncate keep pos size",
                    mr_fs_truncate(&file, 3), 0);
    truncate_expect(__test_status, "tell after truncate", mr_fs_tell(&file), 2);

    truncate_expect(__test_status, "close trunc file", mr_fs_close(&file), 0);

    /* Read-only handle cannot truncate */
    ret = mr_fs_open(&ro, "/trunc", MR_O_RDONLY);
    truncate_expect(__test_status, "open readonly trunc", ret, 0);
    if (ret == 0) {
        truncate_expect(__test_status, "truncate readonly",
                        mr_fs_truncate(&ro, 1), -MR_EBADF);
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
                        mr_fs_truncate(&file, 0), -MR_EISDIR);
        truncate_expect(__test_status, "close trunc-dir", mr_fs_close(&file),
                        0);
    }

    /* Closed handle */
    truncate_expect(__test_status, "truncate closed", mr_fs_truncate(&file, 0),
                    -MR_EBADF);

    /* Seek edges on an empty file */
    ret = mr_fs_open(&file, "/seek-edge", MR_O_RDWR | MR_O_CREAT);
    truncate_expect(__test_status, "create seek-edge", ret, 0);
    if (ret == 0) {
        truncate_expect(__test_status, "seek end empty",
                        mr_fs_seek(&file, 0, MR_SEEK_END), 0);
        truncate_expect(__test_status, "seek cur empty",
                        mr_fs_seek(&file, 0, MR_SEEK_CUR), 0);
        truncate_expect(__test_status, "seek invalid whence",
                        mr_fs_seek(&file, 0, 99), -MR_EINVAL);
        truncate_expect(__test_status, "seek negative clamp",
                        mr_fs_seek(&file, -5, MR_SEEK_SET), 0);
        truncate_expect(__test_status, "close seek-edge", mr_fs_close(&file),
                        0);
    }

_exit:
    truncate_expect(__test_status, "umount ramfs", mr_fs_umount("/"), 0);
    return;
}

MR_TEST(fs, truncate) {
    fs_truncate_test(__test_status);
}

#endif /* defined(MR_USE_TEST) && defined(MR_USE_FS) */
