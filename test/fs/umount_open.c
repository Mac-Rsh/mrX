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

static void umount_open_expect(volatile mr_uint32_t *__test_status,
                               const char *name, mr_int64_t actual,
                               mr_int64_t expected) {
    MR_IGNORE(name);

    MR_TEST_EXPECT_EQ_INT(actual, expected);
}

static void fs_umount_open_test(volatile mr_uint32_t *__test_status) {
    mr_file_t file = MR_FILE_INIT(file);
    mr_file_t dir = MR_FILE_INIT(dir);
    mr_stat_t stat;
    char buffer[8];
    mr_ssize_t io_ret;
    mr_err_t ret;

    /* Nested mount so root can stay free for other tests' assumptions */
    ret = fs_test_mount_root("ramfs", 0, MR_NULL);
    umount_open_expect(__test_status, "mount root", ret, 0);
    if (ret != 0) {
        return;
    }

    umount_open_expect(__test_status, "mkdir nested", mr_fs_mkdir("/nested"),
                       0);
    umount_open_expect(__test_status, "mount nested",
                       mr_fs_mount(MR_NULL, "/nested", "ramfs", 0, MR_NULL), 0);

    /* Create and keep a file open across umount */
    ret = mr_fs_open(&file, "/nested/open-file", MR_O_RDWR | MR_O_CREAT);
    umount_open_expect(__test_status, "create open-file", ret, 0);
    if (ret != 0) {
        goto _exit;
    }
    umount_open_expect(__test_status, "write open-file",
                       mr_fs_write(&file, "data", 4), 4);

    /* Open handles pin the inode/sb by refcount but not op_cnt, so umount
     * is allowed and final kill is deferred until the last put */
    umount_open_expect(__test_status, "umount nested open",
                       mr_fs_umount("/nested"), 0);

    /* Path lookup no longer sees the mount */
    umount_open_expect(__test_status, "path after umount",
                       fs_test_stat("/nested/open-file", &stat), -MR_ENOENT);

    /* The open handle remains usable until close */
    umount_open_expect(__test_status, "stat open after umount",
                       mr_fs_stat(&file, &stat), 0);
    umount_open_expect(__test_status, "size after umount", stat.size, 4);
    umount_open_expect(__test_status, "seek after umount",
                       mr_fs_seek(&file, 0, MR_SEEK_SET), 0);
    io_ret = mr_fs_read(&file, buffer, sizeof(buffer));
    umount_open_expect(__test_status, "read after umount", io_ret, 4);
    if ((io_ret == 4) && (mr_memcmp(buffer, "data", 4) != 0)) {
        MR_TEST_EXPECT_EQ_INT(mr_memcmp(buffer, "data", 4), 0);
    }
    umount_open_expect(__test_status, "write after umount",
                       mr_fs_write(&file, "!", 1), 1);
    umount_open_expect(__test_status, "close after umount", mr_fs_close(&file),
                       0);

    /* Directory handle across umount */
    umount_open_expect(__test_status, "remount nested",
                       mr_fs_mount(MR_NULL, "/nested", "ramfs", 0, MR_NULL), 0);
    umount_open_expect(__test_status, "mkdir nested/d",
                       mr_fs_mkdir("/nested/d"), 0);
    ret = mr_fs_opendir(&dir, "/nested/d");
    umount_open_expect(__test_status, "opendir nested/d", ret, 0);
    if (ret == 0) {
        umount_open_expect(__test_status, "umount with opendir",
                           mr_fs_umount("/nested"), 0);
        umount_open_expect(__test_status, "closedir after umount",
                           mr_fs_closedir(&dir), 0);
    }

    /* Active op_cnt blocks umount of that super-block */
    umount_open_expect(__test_status, "remount for op_cnt",
                       mr_fs_mount(MR_NULL, "/nested", "ramfs", 0, MR_NULL), 0);
    ret = mr_fs_open(&file, "/nested/hold", MR_O_RDWR | MR_O_CREAT);
    umount_open_expect(__test_status, "create hold", ret, 0);
    if (ret == 0) {
        mr_super_block_t *sb = file.inode ? file.inode->sb : MR_NULL;

        if (sb) {
            umount_open_expect(__test_status, "op enter",
                               mr_super_block_op_enter(sb), 0);
            umount_open_expect(__test_status, "umount busy op",
                               mr_fs_umount("/nested"), -MR_EBUSY);
            mr_super_block_op_exit(sb);
            umount_open_expect(__test_status, "umount after op exit",
                               mr_fs_umount("/nested"), 0);
        }
        umount_open_expect(__test_status, "close hold", mr_fs_close(&file), 0);
    }

_exit:
    umount_open_expect(__test_status, "umount root", mr_fs_umount("/"), 0);
    return;
}

MR_TEST(fs, umount_open) {
    fs_umount_open_test(__test_status);
}

#endif /* defined(MR_USE_TEST) && defined(MR_USE_FS) */
