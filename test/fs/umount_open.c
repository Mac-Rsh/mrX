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
#include <mrX/test/fs/fs_test.h>

static void umount_open_expect(volatile mr_uint32_t *__test_status,
                               const char *name, mr_int64_t actual,
                               mr_int64_t expected) {
    MR_IGNORE(name);

    MR_TEST_EXPECT_EQ_INT(actual, expected);
}

MR_TEST(fs, umount_open) {
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

    /* An open file blocks graceful unmount */
    ret = mr_fs_open(&file, "/nested/open-file", MR_O_RDWR | MR_O_CREAT);
    umount_open_expect(__test_status, "create open-file", ret, 0);
    if (ret != 0) {
        goto _exit;
    }
    umount_open_expect(__test_status, "write open-file",
                       mr_fs_write(&file, "data", 4), 4);

    /* Graceful unmount requires every file to be closed */
    umount_open_expect(__test_status, "umount nested open",
                       mr_fs_umount("/nested"), -MR_EBUSY);

    /* The mount and open handle remain usable after the rejected request */
    umount_open_expect(__test_status, "path after busy umount",
                       fs_test_stat("/nested/open-file", &stat), 0);

    umount_open_expect(__test_status, "stat open after busy umount",
                       mr_fs_fstat(&file, &stat), 0);
    umount_open_expect(__test_status, "size after busy umount", stat.size, 4);
    umount_open_expect(__test_status, "seek after busy umount",
                       mr_fs_lseek(&file, 0, MR_SEEK_SET), 0);
    io_ret = mr_fs_read(&file, buffer, sizeof(buffer));
    umount_open_expect(__test_status, "read after busy umount", io_ret, 4);
    if ((io_ret == 4) && (mr_memcmp(buffer, "data", 4) != 0)) {
        MR_TEST_EXPECT_EQ_INT(mr_memcmp(buffer, "data", 4), 0);
    }
    umount_open_expect(__test_status, "write after busy umount",
                       mr_fs_write(&file, "!", 1), 1);
    umount_open_expect(__test_status, "close before umount", mr_fs_close(&file),
                       0);
    umount_open_expect(__test_status, "umount after close",
                       mr_fs_umount("/nested"), 0);

    /* An open directory also blocks graceful unmount */
    umount_open_expect(__test_status, "remount nested",
                       mr_fs_mount(MR_NULL, "/nested", "ramfs", 0, MR_NULL), 0);
    umount_open_expect(__test_status, "mkdir nested/d",
                       mr_fs_mkdir("/nested/d"), 0);
    ret = mr_fs_open(&dir, "/nested/d", MR_O_RDONLY | MR_O_DIRECTORY);
    umount_open_expect(__test_status, "opendir nested/d", ret, 0);
    if (ret == 0) {
        umount_open_expect(__test_status, "umount with opendir",
                           mr_fs_umount("/nested"), -MR_EBUSY);
        umount_open_expect(__test_status, "closedir before umount",
                           mr_fs_close(&dir), 0);
        umount_open_expect(__test_status, "umount after closedir",
                           mr_fs_umount("/nested"), 0);
    }

    /* Active op_cnt blocks umount of that super-block */
    umount_open_expect(__test_status, "remount for op_cnt",
                       mr_fs_mount(MR_NULL, "/nested", "ramfs", 0, MR_NULL), 0);
    ret = mr_fs_open(&file, "/nested/hold", MR_O_RDWR | MR_O_CREAT);
    umount_open_expect(__test_status, "create hold", ret, 0);
    if (ret == 0) {
        mr_sblock_t *sb = file.inode ? file.inode->sb : MR_NULL;

        if (sb) {
            umount_open_expect(__test_status, "op enter",
                               mr_sblock_op_enter(sb), 0);
            umount_open_expect(__test_status, "umount busy op",
                               mr_fs_umount("/nested"), -MR_EBUSY);
            mr_sblock_op_exit(sb);
            umount_open_expect(__test_status, "umount busy open",
                               mr_fs_umount("/nested"), -MR_EBUSY);
        }
        umount_open_expect(__test_status, "close hold", mr_fs_close(&file), 0);
        umount_open_expect(__test_status, "umount after op and close",
                           mr_fs_umount("/nested"), 0);
    }

_exit:
    umount_open_expect(__test_status, "umount root", mr_fs_umount("/"), 0);
    return;
}

#endif /* defined(MR_USE_TEST) && defined(MR_USE_FS) */
