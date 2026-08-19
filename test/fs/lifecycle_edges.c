/**
 * @copyright (c) 2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2026-08-16        MacRsh        First version
 */

#include <mrX/test/mr_test.h>
#if defined(MR_USE_TEST) && defined(MR_USE_FS)
#include <mrX.h>
#include "fs_test.h"

static void lifecycle_edges_expect(volatile mr_uint32_t *__test_status,
                                   const char *name, mr_int64_t actual,
                                   mr_int64_t expected) {
    MR_IGNORE(name);

    MR_TEST_EXPECT_EQ_INT(actual, expected);
}

static void fs_lifecycle_edges_test(volatile mr_uint32_t *__test_status) {
    mr_file_t held = MR_FILE_INIT(held);
    mr_file_t replacement = MR_FILE_INIT(replacement);
    mr_file_t link = MR_FILE_INIT(link);
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
                               mr_fs_unlink(&held), 0);
        lifecycle_edges_expect(__test_status, "held path absent",
                               mr_fs_stat_by_path("/held", &stat), -MR_ENOENT);
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
                               mr_fs_seek(&held, 0, MR_SEEK_SET), 0);
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
                               mr_fs_seek(&replacement, 0, MR_SEEK_SET), 0);
        io_ret = mr_fs_read(&replacement, buffer, sizeof(buffer));
        lifecycle_edges_expect(__test_status, "read replacement", io_ret, 3);
        if ((io_ret == 3) && (mr_memcmp(buffer, "new", 3) != 0)) {
            MR_TEST_EXPECT_EQ_INT(mr_memcmp(buffer, "new", 3), 0);
        }
        lifecycle_edges_expect(__test_status, "close replacement",
                               mr_fs_close(&replacement), 0);
    }

    /* Readlink accepts a zero-sized null buffer and truncates safely */
    lifecycle_edges_expect(__test_status, "create link",
                           mr_fs_symlink("target", "/edge-link"), 0);
    ret = mr_fs_open(&link, "/edge-link", MR_O_RDONLY | MR_O_NOFOLLOW);
    lifecycle_edges_expect(__test_status, "open link nofollow", ret, 0);
    if (ret == 0) {
        lifecycle_edges_expect(__test_status, "readlink zero size",
                               mr_fs_readlink(&link, MR_NULL, 0), 0);
        buffer[0] = '\0';
        lifecycle_edges_expect(__test_status, "readlink truncated",
                               mr_fs_readlink(&link, buffer, 1), 1);
        if (buffer[0] != 't') {
            MR_TEST_EXPECT_EQ_INT(buffer[0], 't');
        }
        lifecycle_edges_expect(__test_status, "readlink exact",
                               mr_fs_readlink(&link, buffer, sizeof(buffer)),
                               6);
        if (mr_memcmp(buffer, "target", 6) != 0) {
            MR_TEST_EXPECT_EQ_INT(mr_memcmp(buffer, "target", 6), 0);
        }
        lifecycle_edges_expect(__test_status, "close link", mr_fs_close(&link),
                               0);
    }
    lifecycle_edges_expect(__test_status, "readlink closed",
                           mr_fs_readlink(&link, buffer, sizeof(buffer)),
                           -MR_EBADF);
    lifecycle_edges_expect(__test_status, "unlink edge link",
                           mr_fs_unlink_by_path("/edge-link"), 0);

    lifecycle_edges_expect(__test_status, "unlink replacement",
                           mr_fs_unlink_by_path("/held"), 0);
    lifecycle_edges_expect(__test_status, "umount ramfs", mr_fs_umount("/"), 0);
    return;
}

MR_TEST(fs, lifecycle_edges) {
    fs_lifecycle_edges_test(__test_status);
}

#endif /* defined(MR_USE_TEST) && defined(MR_USE_FS) */
