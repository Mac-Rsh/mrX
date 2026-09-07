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

static void fs_relative_expect(volatile mr_uint32_t *__test_status,
                               const char *name, mr_int64_t actual,
                               mr_int64_t expected) {
    MR_IGNORE(name);

    MR_TEST_EXPECT_EQ_INT(actual, expected);
}

MR_TEST(fs, relative) {
    mr_file_t file = MR_FILE_INIT(file);
    char cwd[32];
    char cwd_exact[6];
    char cwd_short[5] = {'X', 'X', 'X', 'X', '\0'};
    char root_exact[2];
    char root_short[1] = {'X'};
    mr_stat_t stat;
    mr_err_t ret;

    /* Mount RamFS */
    ret = fs_test_mount_root("ramfs", 0, MR_NULL);
    fs_relative_expect(__test_status, "mount ramfs", ret, 0);
    if (ret != 0) {
        return;
    }

    /* Select a working directory */
    fs_relative_expect(__test_status, "mkdir base", mr_fs_mkdir("/base"), 0);
    fs_relative_expect(__test_status, "chdir base", mr_fs_chdir("/base"), 0);
    fs_relative_expect(__test_status, "getcwd base",
                       mr_fs_getcwd(cwd, sizeof(cwd)), 5);
    if (mr_strcmp(cwd, "/base") != 0) {
        MR_TEST_EXPECT_EQ_INT(mr_strcmp(cwd, "/base"), 0);
    }
    fs_relative_expect(__test_status, "getcwd exact size",
                       mr_fs_getcwd(cwd_exact, sizeof(cwd_exact)), 5);
    if (mr_strcmp(cwd_exact, "/base") != 0) {
        MR_TEST_EXPECT_EQ_INT(mr_strcmp(cwd_exact, "/base"), 0);
    }
    fs_relative_expect(__test_status, "getcwd short size",
                       mr_fs_getcwd(cwd_short, sizeof(cwd_short)), -MR_ERANGE);
    fs_relative_expect(__test_status, "getcwd short keeps data", cwd_short[0],
                       'X');

    /* Relative file operations use CWD */
    ret = mr_fs_open(&file, "file", MR_O_RDWR | MR_O_CREAT);
    fs_relative_expect(__test_status, "create relative file", ret, 0);
    if (ret == 0) {
        fs_relative_expect(__test_status, "write relative file",
                           mr_fs_write(&file, "data", 4), 4);
        fs_relative_expect(__test_status, "close relative file",
                           mr_fs_close(&file), 0);
    }
    fs_relative_expect(__test_status, "stat relative file",
                       fs_test_stat("/base/file", &stat), 0);
    fs_relative_expect(__test_status, "rename relative file",
                       mr_fs_rename("file", "renamed"), 0);
    fs_relative_expect(__test_status, "old relative name absent",
                       fs_test_stat("/base/file", &stat), -MR_ENOENT);

    /* Dot, dot-dot and repeated separators are normalized during lookup */
    fs_relative_expect(__test_status, "mkdir relative directory",
                       mr_fs_mkdir("dir"), 0);
    ret = mr_fs_open(&file, "dir/../renamed", MR_O_RDONLY);
    fs_relative_expect(__test_status, "open through dot-dot", ret, 0);
    if (ret == 0) {
        fs_relative_expect(__test_status, "close dot-dot file",
                           mr_fs_close(&file), 0);
    }
    fs_relative_expect(__test_status, "mkdir missing repeated parent",
                       mr_fs_mkdir("repeat//nested"), -MR_ENOENT);
    fs_relative_expect(__test_status, "mkdir repeated base",
                       mr_fs_mkdir("repeat"), 0);
    fs_relative_expect(__test_status, "mkdir repeated nested",
                       mr_fs_mkdir("repeat//nested"), 0);

    /* CWD can move with relative paths */
    fs_relative_expect(__test_status, "chdir relative directory",
                       mr_fs_chdir("dir"), 0);
    fs_relative_expect(__test_status, "getcwd relative directory",
                       mr_fs_getcwd(cwd, sizeof(cwd)), 9);
    if (mr_strcmp(cwd, "/base/dir") != 0) {
        MR_TEST_EXPECT_EQ_INT(mr_strcmp(cwd, "/base/dir"), 0);
    }
    fs_relative_expect(__test_status, "chdir parent", mr_fs_chdir(".."), 0);

    /* Remove relative namespace entries */
    fs_relative_expect(__test_status, "unlink relative file",
                       mr_fs_unlink("renamed"), 0);
    fs_relative_expect(__test_status, "rmdir relative directory",
                       mr_fs_rmdir("dir"), 0);
    fs_relative_expect(__test_status, "rmdir repeated nested",
                       mr_fs_rmdir("repeat/nested"), 0);
    fs_relative_expect(__test_status, "rmdir repeated base",
                       mr_fs_rmdir("repeat"), 0);
    fs_relative_expect(__test_status, "chdir root", mr_fs_chdir("/"), 0);
    fs_relative_expect(__test_status, "getcwd root exact size",
                       mr_fs_getcwd(root_exact, sizeof(root_exact)), 1);
    if (mr_strcmp(root_exact, "/") != 0) {
        MR_TEST_EXPECT_EQ_INT(mr_strcmp(root_exact, "/"), 0);
    }
    fs_relative_expect(__test_status, "getcwd root short size",
                       mr_fs_getcwd(root_short, sizeof(root_short)), -MR_ERANGE);
    fs_relative_expect(__test_status, "getcwd root short keeps data",
                       root_short[0], 'X');
    fs_relative_expect(__test_status, "rmdir base", mr_fs_rmdir("/base"), 0);
    fs_relative_expect(__test_status, "umount ramfs", mr_fs_umount("/"), 0);
    return;
}

#endif /* defined(MR_USE_TEST) && defined(MR_USE_FS) */
