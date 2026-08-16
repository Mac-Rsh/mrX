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

static int umount_open_expect(const char *name, mr_int64_t actual,
                              mr_int64_t expected) {
    MR_IGNORE(name);

    MR_TEST_EXPECT_EQ_INT(actual, expected);
    return (actual == expected) ? 0 : 1;
}

static int fs_umount_open_test(void) {
    mr_file_t file = MR_FILE_INIT(file);
    mr_file_t dir = MR_FILE_INIT(dir);
    mr_stat_t stat;
    char buffer[8];
    mr_ssize_t io_ret;
    int failures = 0;
    mr_err_t ret;

    /* Nested mount so root can stay free for other tests' assumptions */
    ret = mr_fs_mount(MR_NULL, "/", "ramfs", 0, MR_NULL);
    failures += umount_open_expect("mount root", ret, 0);
    if (ret != 0) {
        return failures;
    }

    failures += umount_open_expect("mkdir nested", mr_fs_mkdir("/nested"), 0);
    failures += umount_open_expect(
        "mount nested",
        mr_fs_mount(MR_NULL, "/nested", "ramfs", 0, MR_NULL), 0);

    /* Create and keep a file open across umount */
    ret = mr_fs_open(&file, "/nested/open-file", MR_O_RDWR | MR_O_CREAT);
    failures += umount_open_expect("create open-file", ret, 0);
    if (ret != 0) {
        goto _exit;
    }
    failures += umount_open_expect("write open-file",
                                   mr_fs_write(&file, "data", 4), 4);

    /* Open handles pin the inode/sb by refcount but not op_cnt, so umount
     * is allowed and final kill is deferred until the last put */
    failures += umount_open_expect("umount nested open",
                                   mr_fs_umount("/nested"), 0);

    /* Path lookup no longer sees the mount */
    failures += umount_open_expect(
        "path after umount", fs_test_stat("/nested/open-file", &stat),
        -MR_ENOENT);

    /* The open handle remains usable until close */
    failures += umount_open_expect("stat open after umount",
                                   mr_fs_stat(&file, &stat), 0);
    failures += umount_open_expect("size after umount", stat.size, 4);
    failures += umount_open_expect("seek after umount",
                                   mr_fs_seek(&file, 0, MR_SEEK_SET), 0);
    io_ret = mr_fs_read(&file, buffer, sizeof(buffer));
    failures += umount_open_expect("read after umount", io_ret, 4);
    if ((io_ret == 4) && (mr_memcmp(buffer, "data", 4) != 0)) {
        MR_TEST_EXPECT_EQ_INT(mr_memcmp(buffer, "data", 4), 0);
        failures++;
    }
    failures += umount_open_expect("write after umount",
                                   mr_fs_write(&file, "!", 1), 1);
    failures += umount_open_expect("close after umount",
                                   mr_fs_close(&file), 0);

    /* Directory handle across umount */
    failures += umount_open_expect(
        "remount nested",
        mr_fs_mount(MR_NULL, "/nested", "ramfs", 0, MR_NULL), 0);
    failures += umount_open_expect("mkdir nested/d",
                                   mr_fs_mkdir("/nested/d"), 0);
    ret = mr_fs_opendir(&dir, "/nested/d");
    failures += umount_open_expect("opendir nested/d", ret, 0);
    if (ret == 0) {
        failures += umount_open_expect("umount with opendir",
                                       mr_fs_umount("/nested"), 0);
        failures += umount_open_expect("closedir after umount",
                                       mr_fs_closedir(&dir), 0);
    }

    /* Active op_cnt blocks umount of that super-block */
    failures += umount_open_expect(
        "remount for op_cnt",
        mr_fs_mount(MR_NULL, "/nested", "ramfs", 0, MR_NULL), 0);
    ret = mr_fs_open(&file, "/nested/hold", MR_O_RDWR | MR_O_CREAT);
    failures += umount_open_expect("create hold", ret, 0);
    if (ret == 0) {
        mr_super_block_t *sb = file.inode ? file.inode->sb : MR_NULL;

        if (sb) {
            failures += umount_open_expect("op enter",
                                           mr_super_block_op_enter(sb), 0);
            failures += umount_open_expect("umount busy op",
                                           mr_fs_umount("/nested"),
                                           -MR_EBUSY);
            mr_super_block_op_exit(sb);
            failures += umount_open_expect("umount after op exit",
                                           mr_fs_umount("/nested"), 0);
        }
        failures += umount_open_expect("close hold", mr_fs_close(&file), 0);
    }

_exit:
    failures += umount_open_expect("umount root", mr_fs_umount("/"), 0);
    return failures;
}

static void umount_open(void) {
    MR_TEST_EXPECT_EQ_INT(fs_umount_open_test(), 0);
}
MR_TEST_EXPORT(fs, umount_open, umount_open);
#endif /* defined(MR_USE_TEST) && defined(MR_USE_FS) */
