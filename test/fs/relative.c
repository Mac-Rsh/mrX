/**
 * @copyright (c) 2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2026-07-11        MacRsh        First version
 */

#include <mrX/test/mr_test.h>
#if defined(MR_USE_TEST) && defined(MR_USE_FS)
#include <mrX.h>
#include "fs_test.h"

static void fs_relative_expect(volatile mr_uint32_t *__test_status,
                               const char *name, mr_err_t actual,
                               mr_err_t expected) {
    MR_IGNORE(name);

    MR_TEST_EXPECT_EQ_INT(actual, expected);
}

static void fs_relative_stat_expect(volatile mr_uint32_t *__test_status,
                                    const char *path, mr_err_t expected) {
    mr_stat_t stat;

    fs_relative_expect(__test_status, path, fs_test_stat(path, &stat),
                       expected);
}

static void fs_relative_test(volatile mr_uint32_t *__test_status) {
    mr_file_t regular = {0};
    mr_file_t dir = {0};
    mr_file_t file = {0};
    mr_stat_t stat;
    char cwd[4];
    mr_err_t ret;

    /* Mount RAMFS */
    ret = fs_test_mount_root("ramfs", 0, MR_NULL);
    fs_relative_expect(__test_status, "mount ramfs", ret, 0);
    if (ret != 0) {
        return;
    }

    /* Set current directory */
    fs_relative_expect(__test_status, "mkdir /a", mr_fs_mkdir("/a"), 0);
    fs_relative_expect(__test_status, "chdir /a", mr_fs_chdir("/a"), 0);

    /* Check getcwd output boundaries */
    fs_relative_expect(__test_status, "getcwd short buffer",
                       (mr_err_t)mr_fs_getcwd(cwd, 2), -MR_ERANGE);
    fs_relative_expect(__test_status, "getcwd exact buffer",
                       (mr_err_t)mr_fs_getcwd(cwd, 3), 2);
    if (mr_strcmp(cwd, "/a") != 0) {
        MR_TEST_EXPECT_EQ_INT(mr_strcmp(cwd, "/a"), 0);
    }

    /* Create slashless paths in cwd */
    fs_relative_expect(__test_status, "mkdir dir", mr_fs_mkdir("dir"), 0);
    fs_relative_stat_expect(__test_status, "/a/dir", 0);
    fs_relative_stat_expect(__test_status, "/dir", -MR_ENOENT);

    ret = mr_fs_open(&file, "file", MR_O_RDWR | MR_O_CREAT);
    fs_relative_expect(__test_status, "open file", ret, 0);
    if (ret == 0) {
        fs_relative_expect(__test_status, "close file", mr_fs_close(&file), 0);
    }
    fs_relative_stat_expect(__test_status, "/a/file", 0);
    fs_relative_stat_expect(__test_status, "/file", -MR_ENOENT);

    ret = mr_fs_open(&file, "old", MR_O_RDWR | MR_O_CREAT);
    fs_relative_expect(__test_status, "open old", ret, 0);
    if (ret == 0) {
        fs_relative_expect(__test_status, "close old", mr_fs_close(&file), 0);
    }
    fs_relative_expect(__test_status, "rename old",
                       fs_test_rename("old", "new"), 0);
    fs_relative_stat_expect(__test_status, "/a/old", -MR_ENOENT);
    fs_relative_stat_expect(__test_status, "/a/new", 0);
    fs_relative_stat_expect(__test_status, "/new", -MR_ENOENT);

    /* Preserve absolute path handling */
    fs_relative_expect(__test_status, "mkdir /absdir", mr_fs_mkdir("/absdir"),
                       0);
    ret = mr_fs_open(&file, "/absfile", MR_O_RDWR | MR_O_CREAT);
    fs_relative_expect(__test_status, "open /absfile", ret, 0);
    if (ret == 0) {
        fs_relative_expect(__test_status, "close /absfile", mr_fs_close(&file),
                           0);
    }
    fs_relative_expect(__test_status, "rename absolute",
                       fs_test_rename("new", "/absnew"), 0);
    fs_relative_stat_expect(__test_status, "/absdir", 0);
    fs_relative_stat_expect(__test_status, "/absfile", 0);
    fs_relative_stat_expect(__test_status, "/absnew", 0);
    fs_relative_stat_expect(__test_status, "/a/new", -MR_ENOENT);

    /* Open paths relative to a directory handle */
    fs_relative_expect(__test_status, "mkdir /base", mr_fs_mkdir("/base"), 0);
    fs_relative_expect(__test_status, "mkdir /base/sub",
                       mr_fs_mkdir("/base/sub"), 0);
    fs_relative_expect(__test_status, "create safe relative symlink",
                       mr_fs_symlink("child", "/base/safe-link"), 0);
    fs_relative_expect(__test_status, "create escaping relative symlink",
                       mr_fs_symlink("../absfile", "/base/escape-link"), 0);
    fs_relative_expect(__test_status, "create escaping absolute symlink",
                       mr_fs_symlink("/absfile", "/base/absolute-link"), 0);
    fs_relative_expect(__test_status, "create nested safe symlink",
                       mr_fs_symlink("../child", "/base/sub/up-link"), 0);
    fs_relative_expect(__test_status, "create nested escaping symlink",
                       mr_fs_symlink("../../absfile", "/base/sub/escape-link"),
                       0);
    ret = mr_fs_opendir(&dir, "/base");
    fs_relative_expect(__test_status, "opendir /base", ret, 0);
    if (ret == 0) {
        ret = mr_fs_openat(&file, &dir, "child", MR_O_RDWR | MR_O_CREAT);
        fs_relative_expect(__test_status, "openat child", ret, 0);
        if (ret == 0) {
            fs_relative_expect(__test_status, "close openat child",
                               mr_fs_close(&file), 0);
        }
        fs_relative_stat_expect(__test_status, "/base/child", 0);
        fs_relative_stat_expect(__test_status, "/a/child", -MR_ENOENT);

        ret = mr_fs_openat(&file, &dir, "child", MR_O_RDONLY);
        fs_relative_expect(__test_status, "openat existing child", ret, 0);
        if (ret == 0) {
            fs_relative_expect(__test_status, "close existing child",
                               mr_fs_close(&file), 0);
        }

        ret = mr_fs_openat(&file, &dir, "sub/nested", MR_O_RDWR | MR_O_CREAT);
        fs_relative_expect(__test_status, "openat nested", ret, 0);
        if (ret == 0) {
            fs_relative_expect(__test_status, "close openat nested",
                               mr_fs_close(&file), 0);
        }
        fs_relative_stat_expect(__test_status, "/base/sub/nested", 0);

        /* Treat the directory handle as a temporary root */
        ret = mr_fs_openat(&file, &dir, "sub/../child",
                           MR_O_RDONLY | MR_AT_IN_ROOT);
        fs_relative_expect(__test_status, "openat in-root safe parent", ret, 0);
        if (ret == 0) {
            fs_relative_expect(__test_status, "close in-root safe parent",
                               mr_fs_close(&file), 0);
        }
        ret = mr_fs_openat(&file, &dir, "../child",
                           MR_O_RDONLY | MR_AT_IN_ROOT);
        fs_relative_expect(__test_status, "openat in-root parent clamp", ret,
                           0);
        if (ret == 0) {
            fs_relative_expect(__test_status, "close in-root parent clamp",
                               mr_fs_close(&file), 0);
        }
        ret = mr_fs_openat(&file, &dir, "../in-root-created",
                           MR_O_RDWR | MR_O_CREAT | MR_AT_IN_ROOT);
        fs_relative_expect(__test_status, "openat in-root create clamp", ret,
                           0);
        if (ret == 0) {
            fs_relative_expect(__test_status, "close in-root created",
                               mr_fs_close(&file), 0);
        }
        fs_relative_stat_expect(__test_status, "/in-root-created", -MR_ENOENT);
        fs_relative_stat_expect(__test_status, "/base/in-root-created", 0);

        ret = mr_fs_openat(&file, &dir, "safe-link",
                           MR_O_RDONLY | MR_AT_IN_ROOT);
        fs_relative_expect(__test_status, "openat in-root safe symlink", ret,
                           0);
        if (ret == 0) {
            fs_relative_expect(__test_status, "close in-root safe symlink",
                               mr_fs_close(&file), 0);
        }
        ret = mr_fs_openat(&file, &dir, "sub/up-link",
                           MR_O_RDONLY | MR_AT_IN_ROOT);
        fs_relative_expect(__test_status, "openat in-root nested symlink", ret,
                           0);
        if (ret == 0) {
            fs_relative_expect(__test_status, "close in-root nested symlink",
                               mr_fs_close(&file), 0);
        }
        fs_relative_expect(__test_status,
                           "openat in-root relative symlink clamp",
                           mr_fs_openat(&file, &dir, "escape-link",
                                        MR_O_RDONLY | MR_AT_IN_ROOT),
                           -MR_ENOENT);
        fs_relative_expect(__test_status, "openat in-root nested symlink clamp",
                           mr_fs_openat(&file, &dir, "sub/escape-link",
                                        MR_O_RDONLY | MR_AT_IN_ROOT),
                           -MR_ENOENT);
        fs_relative_expect(__test_status, "openat in-root absolute symlink",
                           mr_fs_openat(&file, &dir, "absolute-link",
                                        MR_O_RDONLY | MR_AT_IN_ROOT),
                           -MR_ENOENT);

        ret = mr_fs_openat(&file, &dir, "/child", MR_O_RDONLY | MR_AT_IN_ROOT);
        fs_relative_expect(__test_status, "openat in-root absolute", ret, 0);
        if (ret == 0) {
            fs_relative_expect(__test_status, "close in-root absolute",
                               mr_fs_close(&file), 0);
        }

        /* Preserve unrestricted openat parent traversal */
        ret = mr_fs_openat(&file, &dir, "../absfile", MR_O_RDONLY);
        fs_relative_expect(__test_status, "openat unrestricted parent", ret, 0);
        if (ret == 0) {
            fs_relative_expect(__test_status, "close unrestricted parent",
                               mr_fs_close(&file), 0);
        }
    }

    /* Absolute paths ignore the directory handle */
    ret = mr_fs_openat(&file, MR_NULL, "/at-absolute", MR_O_RDWR | MR_O_CREAT);
    fs_relative_expect(__test_status, "openat absolute null dir", ret, 0);
    if (ret == 0) {
        fs_relative_expect(__test_status, "close openat absolute",
                           mr_fs_close(&file), 0);
    }
    fs_relative_stat_expect(__test_status, "/at-absolute", 0);
    fs_relative_expect(
        __test_status, "open in-root without base",
        mr_fs_open(&file, "/absfile", MR_O_RDONLY | MR_AT_IN_ROOT), -MR_EINVAL);

    /* Reject invalid bases only for relative paths */
    fs_relative_expect(__test_status, "openat unopened dir",
                       mr_fs_openat(&file, MR_NULL, "missing", MR_O_RDONLY),
                       -MR_EBADF);
    ret = mr_fs_open(&regular, "/absfile", MR_O_RDONLY);
    fs_relative_expect(__test_status, "open regular base", ret, 0);
    if (ret == 0) {
        fs_relative_expect(__test_status, "openat regular base",
                           mr_fs_openat(&file, &regular, "child", MR_O_RDONLY),
                           -MR_ENOTDIR);
        fs_relative_expect(__test_status, "close regular base",
                           mr_fs_close(&regular), 0);
    }
    /* Directories may have no data file operations, so inode marks an open
     * handle */
    if (dir.inode) {
        fs_relative_expect(__test_status, "closedir /base",
                           mr_fs_closedir(&dir), 0);
    }
    fs_relative_expect(__test_status, "openat closed dir",
                       mr_fs_openat(&file, &dir, "child", MR_O_RDONLY),
                       -MR_EBADF);
    ret = mr_fs_openat(&file, &dir, "/at-closed-absolute",
                       MR_O_RDWR | MR_O_CREAT);
    fs_relative_expect(__test_status, "openat absolute closed dir", ret, 0);
    if (ret == 0) {
        fs_relative_expect(__test_status, "close absolute closed dir",
                           mr_fs_close(&file), 0);
    }
    fs_relative_stat_expect(__test_status, "/at-closed-absolute", 0);

    /* Preserve the boundary when a directory handle becomes a mount-point */
    fs_relative_expect(__test_status, "mkdir late mount-point",
                       mr_fs_mkdir("/late-mount"), 0);
    ret = mr_fs_opendir(&dir, "/late-mount");
    fs_relative_expect(__test_status, "open late mount-point", ret, 0);
    if (ret == 0) {
        ret = mr_fs_mount(MR_NULL, "/late-mount", "ramfs", 0, MR_NULL);
        fs_relative_expect(__test_status, "mount over open directory", ret, 0);
        if (ret == 0) {
            ret = mr_fs_openat(&file, &dir, "child",
                               MR_O_RDWR | MR_O_CREAT | MR_AT_IN_ROOT);
            fs_relative_expect(__test_status, "create below late mount", ret,
                               0);
            if (ret == 0) {
                fs_relative_expect(__test_status, "close late mount child",
                                   mr_fs_close(&file), 0);
            }
            ret = mr_fs_openat(&file, &dir, "..",
                               MR_O_RDONLY | MR_O_DIRECTORY | MR_AT_IN_ROOT);
            fs_relative_expect(__test_status, "clamp late mount parent", ret,
                               0);
            if (ret == 0) {
                fs_relative_expect(__test_status, "close late mount parent",
                                   mr_fs_close(&file), 0);
            }
        }
        fs_relative_expect(__test_status, "close late mount-point",
                           mr_fs_closedir(&dir), 0);
        fs_relative_expect(__test_status, "umount late mount-point",
                           mr_fs_umount("/late-mount"), 0);
    }

    /* Reject namespace operations through a detached directory handle */
    fs_relative_expect(__test_status, "mkdir stale mount-point",
                       mr_fs_mkdir("/stale-mount"), 0);
    ret = mr_fs_mount(MR_NULL, "/stale-mount", "ramfs", 0, MR_NULL);
    fs_relative_expect(__test_status, "mount stale directory", ret, 0);
    if (ret == 0) {
        ret = mr_fs_opendir(&dir, "/stale-mount");
        fs_relative_expect(__test_status, "open stale directory", ret, 0);
        fs_relative_expect(__test_status, "umount stale directory",
                           mr_fs_umount("/stale-mount"), 0);
        if (ret == 0) {
            fs_relative_expect(__test_status, "stat stale directory",
                               mr_fs_stat(&dir, &stat), 0);
            ret = mr_fs_openat(&file, &dir, "detached", MR_O_RDWR | MR_O_CREAT);
            fs_relative_expect(__test_status, "openat stale directory", ret,
                               -MR_ENOENT);
            fs_relative_expect(__test_status, "unlink stale directory",
                               mr_fs_unlink(&dir), -MR_ENOENT);
            fs_relative_expect(__test_status, "rename stale directory",
                               mr_fs_rename(&dir, "/stale-renamed"),
                               -MR_ENOENT);
            fs_relative_expect(__test_status, "close stale directory",
                               mr_fs_closedir(&dir), 0);
        }
    }

    /* Overlay an existing directory and preserve its covered contents */
    fs_relative_expect(
        __test_status, "mount missing directory",
        mr_fs_mount(MR_NULL, "/missing-mount", "ramfs", 0, MR_NULL),
        -MR_ENOENT);
    fs_relative_expect(__test_status, "mkdir /overlay", mr_fs_mkdir("/overlay"),
                       0);
    ret = mr_fs_open(&file, "/overlay/covered", MR_O_RDWR | MR_O_CREAT);
    fs_relative_expect(__test_status, "create covered file", ret, 0);
    if (ret == 0) {
        fs_relative_expect(__test_status, "close covered file",
                           mr_fs_close(&file), 0);
    }
    fs_relative_expect(__test_status, "mount overlay",
                       mr_fs_mount(MR_NULL, "/overlay", "ramfs", 0, MR_NULL),
                       0);
    fs_relative_stat_expect(__test_status, "/overlay/covered", -MR_ENOENT);

    /* Track nested mounts without scanning the dentry tree */
    fs_relative_expect(__test_status, "mkdir nested mount-point",
                       mr_fs_mkdir("/overlay/nested"), 0);
    fs_relative_expect(
        __test_status, "mount nested",
        mr_fs_mount(MR_NULL, "/overlay/nested", "ramfs", 0, MR_NULL), 0);
    fs_relative_expect(__test_status, "umount parent busy",
                       mr_fs_umount("/overlay"), -MR_EBUSY);
    ret = mr_fs_open(&file, "/overlay/nested/child", MR_O_RDWR | MR_O_CREAT);
    fs_relative_expect(__test_status, "create nested child", ret, 0);
    if (ret == 0) {
        fs_relative_expect(__test_status, "close nested child",
                           mr_fs_close(&file), 0);
    }
    fs_relative_expect(__test_status, "umount nested",
                       mr_fs_umount("/overlay/nested"), 0);
    fs_relative_stat_expect(__test_status, "/overlay/nested/child", -MR_ENOENT);
    fs_relative_expect(__test_status, "umount overlay",
                       mr_fs_umount("/overlay"), 0);
    fs_relative_stat_expect(__test_status, "/overlay/covered", 0);

    /* Preserve separator validation */
    fs_relative_expect(__test_status, "mkdir repeat", mr_fs_mkdir("repeat"), 0);
    ret = mr_fs_open(&file, "repeat//file", MR_O_RDWR | MR_O_CREAT);
    fs_relative_expect(__test_status, "open repeat//file", ret, 0);
    if (ret == 0) {
        fs_relative_expect(__test_status, "close repeat//file",
                           mr_fs_close(&file), 0);
    }
    fs_relative_expect(__test_status, "mkdir repeat//dir",
                       mr_fs_mkdir("repeat//dir"), 0);
    fs_relative_expect(__test_status, "rename repeat//file",
                       fs_test_rename("repeat//file", "repeat//new"), 0);
    fs_relative_stat_expect(__test_status, "/a/repeat/new", 0);
    fs_relative_expect(__test_status, "open missing/",
                       mr_fs_open(&file, "missing/", MR_O_RDWR | MR_O_CREAT),
                       -MR_EINVAL);
    fs_relative_expect(__test_status, "mkdir dir/", mr_fs_mkdir("dir/"),
                       -MR_EINVAL);
    fs_relative_expect(__test_status, "rename trailing /",
                       fs_test_rename("/absnew", "move/"), -MR_EINVAL);
    fs_relative_stat_expect(__test_status, "/absnew", 0);

    /* Restore context and unmount */
    fs_relative_expect(__test_status, "chdir /", mr_fs_chdir("/"), 0);
    fs_relative_expect(__test_status, "umount ramfs", mr_fs_umount("/"), 0);
    return;
}

MR_TEST(fs, relative) {
    fs_relative_test(__test_status);
}

#endif /* defined(MR_USE_TEST) && defined(MR_USE_FS) */
