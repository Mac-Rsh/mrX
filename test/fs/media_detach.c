/**
 * @copyright (c) 2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2026-09-01        MacRsh        First version
 */

#include <mrX/test/mr_test.h>
#if defined(MR_USE_TEST) && defined(MR_USE_FS)
#include <mrX.h>
#include <mrX/test/fs/fs_test.h>

static void media_detach_expect(volatile mr_uint32_t *__test_status,
                                const char *name, mr_int64_t actual,
                                mr_int64_t expected) {
    MR_IGNORE(name);

    MR_TEST_EXPECT_EQ_INT(actual, expected);
}

MR_TEST(fs, media_detach) {
    mr_file_t file = MR_FILE_INIT(file);
    mr_stat_t stat;
    char buffer[4];
    mr_err_t ret;

    /* Mount an independent child file system */
    ret = fs_test_mount_root("ramfs", 0, MR_NULL);
    media_detach_expect(__test_status, "mount root", ret, 0);
    if (ret != 0) {
        return;
    }
    media_detach_expect(__test_status, "mkdir media", mr_fs_mkdir("/media"),
                        0);
    media_detach_expect(__test_status, "mount media",
                        mr_fs_mount(MR_NULL, "/media", "ramfs", 0, MR_NULL),
                        0);

    /* Keep a handle alive while the child mount disappears */
    ret = mr_fs_open(&file, "/media/live", MR_O_RDWR | MR_O_CREAT);
    media_detach_expect(__test_status, "open live file", ret, 0);
    if (ret != 0) {
        goto _exit;
    }
    media_detach_expect(__test_status, "write live file",
                        mr_fs_write(&file, "ok", 2), 2);

    /* Media loss hides the child mount and invalidates existing handles */
    media_detach_expect(__test_status, "detach media",
                        mr_fs_media_detach("/media"), 0);
    media_detach_expect(__test_status, "hidden child content",
                        fs_test_stat("/media/live", &stat), -MR_ENOENT);
    media_detach_expect(__test_status, "offline fstat",
                        mr_fs_fstat(&file, &stat), -MR_ENODEV);
    media_detach_expect(__test_status, "offline read",
                        mr_fs_read(&file, buffer, sizeof(buffer)), -MR_ENODEV);
    media_detach_expect(__test_status, "offline write",
                        mr_fs_write(&file, "!", 1), -MR_ENODEV);
    media_detach_expect(__test_status, "offline close", mr_fs_close(&file),
                        -MR_ENODEV);
    media_detach_expect(__test_status, "detached mount absent",
                        mr_fs_umount("/media"), -MR_ENOENT);

_exit:
    if (file.inode) {
        media_detach_expect(__test_status, "close remaining file",
                            mr_fs_close(&file), -MR_ENODEV);
    }
    media_detach_expect(__test_status, "umount root", mr_fs_umount("/"), 0);
}

#endif /* defined(MR_USE_TEST) && defined(MR_USE_FS) */
