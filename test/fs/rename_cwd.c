/**
 * @copyright (c) 2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2026-08-29        MacRsh        First version
 */

#include <mrX/test/mr_test.h>
#if defined(MR_USE_TEST) && defined(MR_USE_FS)
#include <mrX.h>
#include <mrX/test/fs/fs_test.h>

static void rename_cwd_expect(volatile mr_uint32_t *__test_status,
                              const char *name, mr_int64_t actual,
                              mr_int64_t expected) {
    MR_IGNORE(name);

    MR_TEST_EXPECT_EQ_INT(actual, expected);
}

static void rename_cwd_expect_path(volatile mr_uint32_t *__test_status,
                                   const char *name, const char *expected) {
    char cwd[32];
    mr_ssize_t ret;

    ret = mr_fs_getcwd(cwd, sizeof(cwd));
    rename_cwd_expect(__test_status, name, ret,
                      (mr_ssize_t)mr_strlen(expected));
    if ((ret >= 0) && (mr_strcmp(cwd, expected) != 0)) {
        MR_TEST_EXPECT_EQ_INT(mr_strcmp(cwd, expected), 0);
    }
}

/* Test: rename an active ancestor without invalidating CWD or open files */
MR_TEST(fs, rename_cwd) {
    mr_file_t file = MR_FILE_INIT(file);
    mr_stat_t stat;
    char buffer[8];
    mr_ssize_t io_ret;
    mr_err_t ret;
    mr_bool_t mounted = MR_FALSE;

    /* Mount RamFS and prepare two namespace branches */
    ret = fs_test_mount_root("ramfs", 0, MR_NULL);
    rename_cwd_expect(__test_status, "mount ramfs", ret, 0);
    if (ret != 0) {
        return;
    }
    mounted = MR_TRUE;
    ret = mr_fs_mkdir("/tree");
    rename_cwd_expect(__test_status, "mkdir tree", ret, 0);
    if (ret != 0) {
        goto _exit;
    }
    ret = mr_fs_mkdir("/tree/child");
    rename_cwd_expect(__test_status, "mkdir child", ret, 0);
    if (ret != 0) {
        goto _exit;
    }
    ret = mr_fs_mkdir("/outside");
    rename_cwd_expect(__test_status, "mkdir outside", ret, 0);
    if (ret != 0) {
        goto _exit;
    }

    /* Keep an open file inside the active directory */
    ret = mr_fs_chdir("/tree/child");
    rename_cwd_expect(__test_status, "chdir child", ret, 0);
    if (ret != 0) {
        goto _exit;
    }
    rename_cwd_expect_path(__test_status, "cwd before rename", "/tree/child");
    ret = mr_fs_open(&file, "active", MR_O_RDWR | MR_O_CREAT);
    rename_cwd_expect(__test_status, "create active", ret, 0);
    if (ret != 0) {
        goto _exit;
    }
    rename_cwd_expect(__test_status, "write active",
                      mr_fs_write(&file, "seed", 4), 4);

    /* Moving an ancestor updates the visible CWD without losing its identity */
    rename_cwd_expect(__test_status, "rename active ancestor",
                      mr_fs_rename("/tree", "/renamed"), 0);
    rename_cwd_expect_path(__test_status, "cwd after ancestor rename",
                           "/renamed/child");

    /* Move the open file out of CWD and retain access through its handle */
    rename_cwd_expect(__test_status, "move open file",
                      mr_fs_rename("active", "/outside/moved"), 0);
    rename_cwd_expect(__test_status, "old active name absent",
                      fs_test_stat("active", &stat), -MR_ENOENT);
    rename_cwd_expect(__test_status, "moved file present",
                      fs_test_stat("/outside/moved", &stat), 0);
    rename_cwd_expect_path(__test_status, "cwd after file move",
                           "/renamed/child");
    rename_cwd_expect(__test_status, "seek moved open file",
                      mr_fs_lseek(&file, 0, MR_SEEK_SET), 0);
    io_ret = mr_fs_read(&file, buffer, sizeof(buffer));
    rename_cwd_expect(__test_status, "read moved open file", io_ret, 4);
    if ((io_ret == 4) && (mr_memcmp(buffer, "seed", 4) != 0)) {
        MR_TEST_EXPECT_EQ_INT(mr_memcmp(buffer, "seed", 4), 0);
    }
    rename_cwd_expect(__test_status, "close moved open file",
                      mr_fs_close(&file), 0);

    /* Remove the moved namespace while preserving normal cleanup order */
    rename_cwd_expect(__test_status, "chdir root", mr_fs_chdir("/"), 0);
    rename_cwd_expect(__test_status, "rmdir moved child",
                      mr_fs_rmdir("/renamed/child"), 0);
    rename_cwd_expect(__test_status, "rmdir renamed tree",
                      mr_fs_rmdir("/renamed"), 0);
    rename_cwd_expect(__test_status, "unlink moved file",
                      mr_fs_unlink("/outside/moved"), 0);
    rename_cwd_expect(__test_status, "rmdir outside", mr_fs_rmdir("/outside"),
                      0);

_exit:
    if (file.inode) {
        rename_cwd_expect(__test_status, "close active on cleanup",
                          mr_fs_close(&file), 0);
    }
    if (mounted) {
        rename_cwd_expect(__test_status, "cleanup chdir root", mr_fs_chdir("/"),
                          0);
        rename_cwd_expect(__test_status, "umount ramfs", mr_fs_umount("/"), 0);
    }
}

#endif /* defined(MR_USE_TEST) && defined(MR_USE_FS) */
