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

static int truncate_expect(const char *name, mr_int64_t actual,
                           mr_int64_t expected) {
    MR_IGNORE(name);

    MR_TEST_EXPECT_EQ_INT(actual, expected);
    return (actual == expected) ? 0 : 1;
}

static int fs_truncate_test(void) {
    mr_file_t file = MR_FILE_INIT(file);
    mr_file_t ro = MR_FILE_INIT(ro);
    mr_stat_t stat;
    char buffer[16];
    mr_ssize_t io_ret;
    int failures = 0;
    mr_err_t ret;

    /* Mount RamFS */
    ret = mr_fs_mount(MR_NULL, "/", "ramfs", 0, MR_NULL);
    failures += truncate_expect("mount ramfs", ret, 0);
    if (ret != 0) {
        return failures;
    }

    /* Seed a regular file */
    ret = mr_fs_open(&file, "/trunc", MR_O_RDWR | MR_O_CREAT);
    failures += truncate_expect("create trunc file", ret, 0);
    if (ret != 0) {
        goto _exit;
    }
    failures += truncate_expect("write seed",
                                mr_fs_write(&file, "0123456789", 10), 10);

    /* Same-size truncate is a no-op */
    failures += truncate_expect("truncate same size",
                                mr_fs_truncate(&file, 10), 0);
    failures += truncate_expect("stat same size", mr_fs_stat(&file, &stat), 0);
    failures += truncate_expect("same size value", stat.size, 10);

    /* Shrink */
    failures += truncate_expect("truncate shrink",
                                mr_fs_truncate(&file, 4), 0);
    failures += truncate_expect("stat shrink", mr_fs_stat(&file, &stat), 0);
    failures += truncate_expect("shrink size", stat.size, 4);
    failures += truncate_expect("seek shrink head",
                                mr_fs_seek(&file, 0, MR_SEEK_SET), 0);
    io_ret = mr_fs_read(&file, buffer, sizeof(buffer));
    failures += truncate_expect("read shrink", io_ret, 4);
    if ((io_ret == 4) && (mr_memcmp(buffer, "0123", 4) != 0)) {
        MR_TEST_EXPECT_EQ_INT(mr_memcmp(buffer, "0123", 4), 0);
        failures++;
    }

    /* Grow with zero fill */
    failures += truncate_expect("truncate grow",
                                mr_fs_truncate(&file, 8), 0);
    failures += truncate_expect("stat grow", mr_fs_stat(&file, &stat), 0);
    failures += truncate_expect("grow size", stat.size, 8);
    failures += truncate_expect("seek grow head",
                                mr_fs_seek(&file, 0, MR_SEEK_SET), 0);
    io_ret = mr_fs_read(&file, buffer, sizeof(buffer));
    failures += truncate_expect("read grow", io_ret, 8);
    if ((io_ret == 8) && (mr_memcmp(buffer, "0123\0\0\0\0", 8) != 0)) {
        MR_TEST_EXPECT_EQ_INT(mr_memcmp(buffer, "0123\0\0\0\0", 8), 0);
        failures++;
    }

    /* Truncate to zero */
    failures += truncate_expect("truncate zero",
                                mr_fs_truncate(&file, 0), 0);
    failures += truncate_expect("stat zero", mr_fs_stat(&file, &stat), 0);
    failures += truncate_expect("zero size", stat.size, 0);
    failures += truncate_expect("seek zero head",
                                mr_fs_seek(&file, 0, MR_SEEK_SET), 0);
    failures += truncate_expect("read empty",
                                mr_fs_read(&file, buffer, sizeof(buffer)), 0);

    /* Hole write beyond EOF zero-fills the gap */
    failures += truncate_expect("seek hole",
                                mr_fs_seek(&file, 4, MR_SEEK_SET), 4);
    failures += truncate_expect("write hole",
                                mr_fs_write(&file, "AB", 2), 2);
    failures += truncate_expect("stat hole", mr_fs_stat(&file, &stat), 0);
    failures += truncate_expect("hole size", stat.size, 6);
    failures += truncate_expect("seek hole head",
                                mr_fs_seek(&file, 0, MR_SEEK_SET), 0);
    io_ret = mr_fs_read(&file, buffer, sizeof(buffer));
    failures += truncate_expect("read hole", io_ret, 6);
    if ((io_ret == 6) && (mr_memcmp(buffer, "\0\0\0\0AB", 6) != 0)) {
        MR_TEST_EXPECT_EQ_INT(mr_memcmp(buffer, "\0\0\0\0AB", 6), 0);
        failures++;
    }

    /* Negative length is invalid */
    failures += truncate_expect("truncate negative",
                                mr_fs_truncate(&file, -1), -MR_EINVAL);

    /* Truncate does not change the file position */
    failures += truncate_expect("seek mid",
                                mr_fs_seek(&file, 2, MR_SEEK_SET), 2);
    failures += truncate_expect("truncate keep pos size",
                                mr_fs_truncate(&file, 3), 0);
    failures += truncate_expect("tell after truncate", mr_fs_tell(&file), 2);

    failures += truncate_expect("close trunc file", mr_fs_close(&file), 0);

    /* Read-only handle cannot truncate */
    ret = mr_fs_open(&ro, "/trunc", MR_O_RDONLY);
    failures += truncate_expect("open readonly trunc", ret, 0);
    if (ret == 0) {
        failures += truncate_expect("truncate readonly",
                                    mr_fs_truncate(&ro, 1), -MR_EBADF);
        failures += truncate_expect("close readonly trunc",
                                    mr_fs_close(&ro), 0);
    }

    /* Directory truncate is rejected */
    failures += truncate_expect("mkdir trunc-dir",
                                mr_fs_mkdir("/trunc-dir"), 0);
    ret = mr_fs_open(&file, "/trunc-dir", MR_O_RDONLY);
    failures += truncate_expect("open trunc-dir", ret, 0);
    if (ret == 0) {
        failures += truncate_expect("truncate directory",
                                    mr_fs_truncate(&file, 0), -MR_EISDIR);
        failures += truncate_expect("close trunc-dir",
                                    mr_fs_close(&file), 0);
    }

    /* Closed handle */
    failures += truncate_expect("truncate closed",
                                mr_fs_truncate(&file, 0), -MR_EBADF);

    /* Seek edges on an empty file */
    ret = mr_fs_open(&file, "/seek-edge", MR_O_RDWR | MR_O_CREAT);
    failures += truncate_expect("create seek-edge", ret, 0);
    if (ret == 0) {
        failures += truncate_expect("seek end empty",
                                    mr_fs_seek(&file, 0, MR_SEEK_END), 0);
        failures += truncate_expect("seek cur empty",
                                    mr_fs_seek(&file, 0, MR_SEEK_CUR), 0);
        failures += truncate_expect("seek invalid whence",
                                    mr_fs_seek(&file, 0, 99), -MR_EINVAL);
        failures += truncate_expect("seek negative clamp",
                                    mr_fs_seek(&file, -5, MR_SEEK_SET), 0);
        failures += truncate_expect("close seek-edge",
                                    mr_fs_close(&file), 0);
    }

_exit:
    failures += truncate_expect("umount ramfs", mr_fs_umount("/"), 0);
    return failures;
}

static void truncate(void) {
    MR_TEST_EXPECT_EQ_INT(fs_truncate_test(), 0);
}
MR_TEST_EXPORT(fs, truncate, truncate);
#endif /* defined(MR_USE_TEST) && defined(MR_USE_FS) */
