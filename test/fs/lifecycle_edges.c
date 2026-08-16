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

static int lifecycle_edges_expect(const char *name, mr_int64_t actual,
                                  mr_int64_t expected) {
    MR_IGNORE(name);

    MR_TEST_EXPECT_EQ_INT(actual, expected);
    return (actual == expected) ? 0 : 1;
}

static int fs_lifecycle_edges_test(void) {
    mr_file_t held = MR_FILE_INIT(held);
    mr_file_t replacement = MR_FILE_INIT(replacement);
    mr_file_t link = MR_FILE_INIT(link);
    mr_stat_t stat;
    char buffer[8];
    mr_ssize_t io_ret;
    int failures = 0;
    mr_err_t ret;

    /* Mount RamFS */
    ret = mr_fs_mount(MR_NULL, "/", "ramfs", 0, MR_NULL);
    failures += lifecycle_edges_expect("mount ramfs", ret, 0);
    if (ret != 0) {
        return failures;
    }

    /* An unlinked open file remains usable until its last handle is closed */
    ret = mr_fs_open(&held, "/held", MR_O_RDWR | MR_O_CREAT);
    failures += lifecycle_edges_expect("create held file", ret, 0);
    if (ret == 0) {
        failures += lifecycle_edges_expect("write held file",
                                           mr_fs_write(&held, "old", 3), 3);
        failures += lifecycle_edges_expect("unlink held file",
                                           mr_fs_unlink(&held), 0);
        failures += lifecycle_edges_expect(
            "held path absent", mr_fs_stat_by_path("/held", &stat), -MR_ENOENT);
    }

    /* Recreating the pathname must produce a separate inode and data stream */
    ret = mr_fs_open(&replacement, "/held", MR_O_RDWR | MR_O_CREAT | MR_O_EXCL);
    failures += lifecycle_edges_expect("recreate held path", ret, 0);
    if (ret == 0) {
        failures += lifecycle_edges_expect(
            "write replacement", mr_fs_write(&replacement, "new", 3), 3);
    }

    if (held.inode) {
        failures += lifecycle_edges_expect(
            "seek unlinked held", mr_fs_seek(&held, 0, MR_SEEK_SET), 0);
        io_ret = mr_fs_read(&held, buffer, sizeof(buffer));
        failures += lifecycle_edges_expect("read unlinked held", io_ret, 3);
        if ((io_ret == 3) && (mr_memcmp(buffer, "old", 3) != 0)) {
            MR_TEST_EXPECT_EQ_INT(mr_memcmp(buffer, "old", 3), 0);
            failures++;
        }
        failures += lifecycle_edges_expect("close unlinked held",
                                           mr_fs_close(&held), 0);
    }
    if (replacement.inode) {
        failures += lifecycle_edges_expect(
            "seek replacement", mr_fs_seek(&replacement, 0, MR_SEEK_SET), 0);
        io_ret = mr_fs_read(&replacement, buffer, sizeof(buffer));
        failures += lifecycle_edges_expect("read replacement", io_ret, 3);
        if ((io_ret == 3) && (mr_memcmp(buffer, "new", 3) != 0)) {
            MR_TEST_EXPECT_EQ_INT(mr_memcmp(buffer, "new", 3), 0);
            failures++;
        }
        failures += lifecycle_edges_expect("close replacement",
                                           mr_fs_close(&replacement), 0);
    }

    /* Readlink accepts a zero-sized null buffer and truncates safely */
    failures += lifecycle_edges_expect(
        "create link", mr_fs_symlink("target", "/edge-link"), 0);
    ret = mr_fs_open(&link, "/edge-link", MR_O_RDONLY | MR_O_NOFOLLOW);
    failures += lifecycle_edges_expect("open link nofollow", ret, 0);
    if (ret == 0) {
        failures += lifecycle_edges_expect(
            "readlink zero size", mr_fs_readlink(&link, MR_NULL, 0), 0);
        buffer[0] = '\0';
        failures += lifecycle_edges_expect("readlink truncated",
                                           mr_fs_readlink(&link, buffer, 1), 1);
        if (buffer[0] != 't') {
            MR_TEST_EXPECT_EQ_INT(buffer[0], 't');
            failures++;
        }
        failures += lifecycle_edges_expect(
            "readlink exact", mr_fs_readlink(&link, buffer, sizeof(buffer)), 6);
        if (mr_memcmp(buffer, "target", 6) != 0) {
            MR_TEST_EXPECT_EQ_INT(mr_memcmp(buffer, "target", 6), 0);
            failures++;
        }
        failures += lifecycle_edges_expect("close link", mr_fs_close(&link), 0);
    }
    failures += lifecycle_edges_expect(
        "readlink closed", mr_fs_readlink(&link, buffer, sizeof(buffer)),
        -MR_EBADF);
    failures += lifecycle_edges_expect("unlink edge link",
                                       mr_fs_unlink_by_path("/edge-link"), 0);

    failures += lifecycle_edges_expect("unlink replacement",
                                       mr_fs_unlink_by_path("/held"), 0);
    failures += lifecycle_edges_expect("umount ramfs", mr_fs_umount("/"), 0);
    return failures;
}

static void lifecycle_edges(void) {
    MR_TEST_EXPECT_EQ_INT(fs_lifecycle_edges_test(), 0);
}
MR_TEST_EXPORT(fs, lifecycle_edges, lifecycle_edges);
#endif /* defined(MR_USE_TEST) && defined(MR_USE_FS) */
