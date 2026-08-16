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

static int fs_relative_expect(const char *name, mr_err_t actual,
                              mr_err_t expected) {
    MR_IGNORE(name);

    MR_TEST_EXPECT_EQ_INT(actual, expected);
    return (actual == expected) ? 0 : 1;
}

static int fs_relative_stat_expect(const char *path, mr_err_t expected) {
    mr_stat_t stat;

    return fs_relative_expect(path, fs_test_stat(path, &stat), expected);
}

static int fs_relative_test(void) {
    mr_file_t regular = {0};
    mr_file_t dir = {0};
    mr_file_t file = {0};
    mr_stat_t stat;
    char cwd[4];
    int failures = 0;
    mr_err_t ret;

    /* Mount RAMFS */
    ret = mr_fs_mount(MR_NULL, "/", "ramfs", 0, MR_NULL);
    failures += fs_relative_expect("mount ramfs", ret, 0);
    if (ret != 0) {
        return failures;
    }

    /* Set current directory */
    failures += fs_relative_expect("mkdir /a", mr_fs_mkdir("/a"), 0);
    failures += fs_relative_expect("chdir /a", mr_fs_chdir("/a"), 0);

    /* Check getcwd output boundaries */
    failures += fs_relative_expect("getcwd short buffer",
                                   (mr_err_t)mr_fs_getcwd(cwd, 2), -MR_ERANGE);
    failures += fs_relative_expect("getcwd exact buffer",
                                   (mr_err_t)mr_fs_getcwd(cwd, 3), 2);
    if (mr_strcmp(cwd, "/a") != 0) {
        MR_TEST_EXPECT_EQ_INT(mr_strcmp(cwd, "/a"), 0);
        failures++;
    }

    /* Create slashless paths in cwd */
    failures += fs_relative_expect("mkdir dir", mr_fs_mkdir("dir"), 0);
    failures += fs_relative_stat_expect("/a/dir", 0);
    failures += fs_relative_stat_expect("/dir", -MR_ENOENT);

    ret = mr_fs_open(&file, "file", MR_O_RDWR | MR_O_CREAT);
    failures += fs_relative_expect("open file", ret, 0);
    if (ret == 0) {
        failures += fs_relative_expect("close file", mr_fs_close(&file), 0);
    }
    failures += fs_relative_stat_expect("/a/file", 0);
    failures += fs_relative_stat_expect("/file", -MR_ENOENT);

    ret = mr_fs_open(&file, "old", MR_O_RDWR | MR_O_CREAT);
    failures += fs_relative_expect("open old", ret, 0);
    if (ret == 0) {
        failures += fs_relative_expect("close old", mr_fs_close(&file), 0);
    }
    failures
        += fs_relative_expect("rename old", fs_test_rename("old", "new"), 0);
    failures += fs_relative_stat_expect("/a/old", -MR_ENOENT);
    failures += fs_relative_stat_expect("/a/new", 0);
    failures += fs_relative_stat_expect("/new", -MR_ENOENT);

    /* Preserve absolute path handling */
    failures += fs_relative_expect("mkdir /absdir", mr_fs_mkdir("/absdir"), 0);
    ret = mr_fs_open(&file, "/absfile", MR_O_RDWR | MR_O_CREAT);
    failures += fs_relative_expect("open /absfile", ret, 0);
    if (ret == 0) {
        failures += fs_relative_expect("close /absfile", mr_fs_close(&file), 0);
    }
    failures += fs_relative_expect("rename absolute",
                                   fs_test_rename("new", "/absnew"), 0);
    failures += fs_relative_stat_expect("/absdir", 0);
    failures += fs_relative_stat_expect("/absfile", 0);
    failures += fs_relative_stat_expect("/absnew", 0);
    failures += fs_relative_stat_expect("/a/new", -MR_ENOENT);

    /* Open paths relative to a directory handle */
    failures += fs_relative_expect("mkdir /base", mr_fs_mkdir("/base"), 0);
    failures
        += fs_relative_expect("mkdir /base/sub", mr_fs_mkdir("/base/sub"), 0);
    failures += fs_relative_expect(
        "create safe relative symlink",
        mr_fs_symlink("child", "/base/safe-link"), 0);
    failures += fs_relative_expect(
        "create escaping relative symlink",
        mr_fs_symlink("../absfile", "/base/escape-link"), 0);
    failures += fs_relative_expect(
        "create escaping absolute symlink",
        mr_fs_symlink("/absfile", "/base/absolute-link"), 0);
    failures += fs_relative_expect(
        "create nested safe symlink",
        mr_fs_symlink("../child", "/base/sub/up-link"), 0);
    failures += fs_relative_expect(
        "create nested escaping symlink",
        mr_fs_symlink("../../absfile", "/base/sub/escape-link"), 0);
    ret = mr_fs_opendir(&dir, "/base");
    failures += fs_relative_expect("opendir /base", ret, 0);
    if (ret == 0) {
        ret = mr_fs_openat(&file, &dir, "child", MR_O_RDWR | MR_O_CREAT);
        failures += fs_relative_expect("openat child", ret, 0);
        if (ret == 0) {
            failures += fs_relative_expect("close openat child",
                                           mr_fs_close(&file), 0);
        }
        failures += fs_relative_stat_expect("/base/child", 0);
        failures += fs_relative_stat_expect("/a/child", -MR_ENOENT);

        ret = mr_fs_openat(&file, &dir, "child", MR_O_RDONLY);
        failures += fs_relative_expect("openat existing child", ret, 0);
        if (ret == 0) {
            failures += fs_relative_expect("close existing child",
                                           mr_fs_close(&file), 0);
        }

        ret = mr_fs_openat(&file, &dir, "sub/nested", MR_O_RDWR | MR_O_CREAT);
        failures += fs_relative_expect("openat nested", ret, 0);
        if (ret == 0) {
            failures += fs_relative_expect("close openat nested",
                                           mr_fs_close(&file), 0);
        }
        failures += fs_relative_stat_expect("/base/sub/nested", 0);

        /* Treat the directory handle as a temporary root */
        ret = mr_fs_openat(&file, &dir, "sub/../child",
                           MR_O_RDONLY | MR_AT_IN_ROOT);
        failures += fs_relative_expect("openat in-root safe parent", ret, 0);
        if (ret == 0) {
            failures += fs_relative_expect("close in-root safe parent",
                                           mr_fs_close(&file), 0);
        }
        ret = mr_fs_openat(&file, &dir, "../child",
                           MR_O_RDONLY | MR_AT_IN_ROOT);
        failures += fs_relative_expect("openat in-root parent clamp", ret, 0);
        if (ret == 0) {
            failures += fs_relative_expect("close in-root parent clamp",
                                           mr_fs_close(&file), 0);
        }
        ret = mr_fs_openat(&file, &dir, "../in-root-created",
                           MR_O_RDWR | MR_O_CREAT | MR_AT_IN_ROOT);
        failures += fs_relative_expect("openat in-root create clamp", ret, 0);
        if (ret == 0) {
            failures += fs_relative_expect("close in-root created",
                                           mr_fs_close(&file), 0);
        }
        failures += fs_relative_stat_expect("/in-root-created", -MR_ENOENT);
        failures += fs_relative_stat_expect("/base/in-root-created", 0);

        ret = mr_fs_openat(&file, &dir, "safe-link",
                           MR_O_RDONLY | MR_AT_IN_ROOT);
        failures += fs_relative_expect("openat in-root safe symlink", ret, 0);
        if (ret == 0) {
            failures += fs_relative_expect("close in-root safe symlink",
                                           mr_fs_close(&file), 0);
        }
        ret = mr_fs_openat(&file, &dir, "sub/up-link",
                           MR_O_RDONLY | MR_AT_IN_ROOT);
        failures += fs_relative_expect("openat in-root nested symlink", ret,
                                       0);
        if (ret == 0) {
            failures += fs_relative_expect("close in-root nested symlink",
                                           mr_fs_close(&file), 0);
        }
        failures += fs_relative_expect(
            "openat in-root relative symlink clamp",
            mr_fs_openat(&file, &dir, "escape-link",
                         MR_O_RDONLY | MR_AT_IN_ROOT),
            -MR_ENOENT);
        failures += fs_relative_expect(
            "openat in-root nested symlink clamp",
            mr_fs_openat(&file, &dir, "sub/escape-link",
                         MR_O_RDONLY | MR_AT_IN_ROOT),
            -MR_ENOENT);
        failures += fs_relative_expect(
            "openat in-root absolute symlink",
            mr_fs_openat(&file, &dir, "absolute-link",
                         MR_O_RDONLY | MR_AT_IN_ROOT),
            -MR_ENOENT);

        ret = mr_fs_openat(&file, &dir, "/child",
                           MR_O_RDONLY | MR_AT_IN_ROOT);
        failures += fs_relative_expect("openat in-root absolute", ret, 0);
        if (ret == 0) {
            failures += fs_relative_expect("close in-root absolute",
                                           mr_fs_close(&file), 0);
        }

        /* Preserve unrestricted openat parent traversal */
        ret = mr_fs_openat(&file, &dir, "../absfile", MR_O_RDONLY);
        failures += fs_relative_expect("openat unrestricted parent", ret, 0);
        if (ret == 0) {
            failures += fs_relative_expect("close unrestricted parent",
                                           mr_fs_close(&file), 0);
        }
    }

    /* Absolute paths ignore the directory handle */
    ret = mr_fs_openat(&file, MR_NULL, "/at-absolute", MR_O_RDWR | MR_O_CREAT);
    failures += fs_relative_expect("openat absolute null dir", ret, 0);
    if (ret == 0) {
        failures += fs_relative_expect("close openat absolute",
                                       mr_fs_close(&file), 0);
    }
    failures += fs_relative_stat_expect("/at-absolute", 0);
    failures += fs_relative_expect(
        "open in-root without base",
        mr_fs_open(&file, "/absfile", MR_O_RDONLY | MR_AT_IN_ROOT),
        -MR_EINVAL);

    /* Reject invalid bases only for relative paths */
    failures += fs_relative_expect(
        "openat unopened dir",
        mr_fs_openat(&file, MR_NULL, "missing", MR_O_RDONLY), -MR_EBADF);
    ret = mr_fs_open(&regular, "/absfile", MR_O_RDONLY);
    failures += fs_relative_expect("open regular base", ret, 0);
    if (ret == 0) {
        failures += fs_relative_expect(
            "openat regular base",
            mr_fs_openat(&file, &regular, "child", MR_O_RDONLY), -MR_ENOTDIR);
        failures += fs_relative_expect("close regular base",
                                       mr_fs_close(&regular), 0);
    }
    if (dir.fops) {
        failures
            += fs_relative_expect("closedir /base", mr_fs_closedir(&dir), 0);
    }
    failures += fs_relative_expect(
        "openat closed dir", mr_fs_openat(&file, &dir, "child", MR_O_RDONLY),
        -MR_EBADF);
    ret = mr_fs_openat(&file, &dir, "/at-closed-absolute",
                       MR_O_RDWR | MR_O_CREAT);
    failures += fs_relative_expect("openat absolute closed dir", ret, 0);
    if (ret == 0) {
        failures += fs_relative_expect("close absolute closed dir",
                                       mr_fs_close(&file), 0);
    }
    failures += fs_relative_stat_expect("/at-closed-absolute", 0);

    /* Preserve the boundary when a directory handle becomes a mount-point */
    failures += fs_relative_expect("mkdir late mount-point",
                                   mr_fs_mkdir("/late-mount"), 0);
    ret = mr_fs_opendir(&dir, "/late-mount");
    failures += fs_relative_expect("open late mount-point", ret, 0);
    if (ret == 0) {
        ret = mr_fs_mount(MR_NULL, "/late-mount", "ramfs", 0, MR_NULL);
        failures += fs_relative_expect("mount over open directory", ret, 0);
        if (ret == 0) {
            ret = mr_fs_openat(&file, &dir, "child",
                               MR_O_RDWR | MR_O_CREAT | MR_AT_IN_ROOT);
            failures += fs_relative_expect("create below late mount", ret, 0);
            if (ret == 0) {
                failures += fs_relative_expect("close late mount child",
                                               mr_fs_close(&file), 0);
            }
            ret = mr_fs_openat(&file, &dir, "..",
                               MR_O_RDONLY | MR_O_DIRECTORY | MR_AT_IN_ROOT);
            failures += fs_relative_expect("clamp late mount parent", ret, 0);
            if (ret == 0) {
                failures += fs_relative_expect("close late mount parent",
                                               mr_fs_close(&file), 0);
            }
        }
        failures += fs_relative_expect("close late mount-point",
                                       mr_fs_closedir(&dir), 0);
        failures += fs_relative_expect("umount late mount-point",
                                       mr_fs_umount("/late-mount"), 0);
    }

    /* Reject namespace operations through a detached directory handle */
    failures += fs_relative_expect("mkdir stale mount-point",
                                   mr_fs_mkdir("/stale-mount"), 0);
    ret = mr_fs_mount(MR_NULL, "/stale-mount", "ramfs", 0, MR_NULL);
    failures += fs_relative_expect("mount stale directory", ret, 0);
    if (ret == 0) {
        ret = mr_fs_opendir(&dir, "/stale-mount");
        failures += fs_relative_expect("open stale directory", ret, 0);
        failures += fs_relative_expect("umount stale directory",
                                       mr_fs_umount("/stale-mount"), 0);
        if (ret == 0) {
            failures += fs_relative_expect("stat stale directory",
                                           mr_fs_stat(&dir, &stat), 0);
            ret = mr_fs_openat(&file, &dir, "detached", MR_O_RDWR | MR_O_CREAT);
            failures += fs_relative_expect("openat stale directory", ret,
                                           -MR_ENOENT);
            failures += fs_relative_expect("unlink stale directory",
                                           mr_fs_unlink(&dir), -MR_ENOENT);
            failures += fs_relative_expect("rename stale directory",
                                           mr_fs_rename(&dir, "/stale-renamed"),
                                           -MR_ENOENT);
            failures += fs_relative_expect("close stale directory",
                                           mr_fs_closedir(&dir), 0);
        }
    }

    /* Overlay an existing directory and preserve its covered contents */
    failures += fs_relative_expect(
        "mount missing directory",
        mr_fs_mount(MR_NULL, "/missing-mount", "ramfs", 0, MR_NULL),
        -MR_ENOENT);
    failures
        += fs_relative_expect("mkdir /overlay", mr_fs_mkdir("/overlay"), 0);
    ret = mr_fs_open(&file, "/overlay/covered", MR_O_RDWR | MR_O_CREAT);
    failures += fs_relative_expect("create covered file", ret, 0);
    if (ret == 0) {
        failures
            += fs_relative_expect("close covered file", mr_fs_close(&file), 0);
    }
    failures += fs_relative_expect(
        "mount overlay", mr_fs_mount(MR_NULL, "/overlay", "ramfs", 0, MR_NULL),
        0);
    failures += fs_relative_stat_expect("/overlay/covered", -MR_ENOENT);

    /* Track nested mounts without scanning the dentry tree */
    failures += fs_relative_expect("mkdir nested mount-point",
                                   mr_fs_mkdir("/overlay/nested"), 0);
    failures += fs_relative_expect(
        "mount nested",
        mr_fs_mount(MR_NULL, "/overlay/nested", "ramfs", 0, MR_NULL), 0);
    failures += fs_relative_expect("umount parent busy",
                                   mr_fs_umount("/overlay"), -MR_EBUSY);
    ret = mr_fs_open(&file, "/overlay/nested/child", MR_O_RDWR | MR_O_CREAT);
    failures += fs_relative_expect("create nested child", ret, 0);
    if (ret == 0) {
        failures
            += fs_relative_expect("close nested child", mr_fs_close(&file), 0);
    }
    failures += fs_relative_expect("umount nested",
                                   mr_fs_umount("/overlay/nested"), 0);
    failures += fs_relative_stat_expect("/overlay/nested/child", -MR_ENOENT);
    failures
        += fs_relative_expect("umount overlay", mr_fs_umount("/overlay"), 0);
    failures += fs_relative_stat_expect("/overlay/covered", 0);

    /* Preserve separator validation */
    failures += fs_relative_expect("mkdir repeat", mr_fs_mkdir("repeat"), 0);
    ret = mr_fs_open(&file, "repeat//file", MR_O_RDWR | MR_O_CREAT);
    failures += fs_relative_expect("open repeat//file", ret, 0);
    if (ret == 0) {
        failures
            += fs_relative_expect("close repeat//file", mr_fs_close(&file), 0);
    }
    failures += fs_relative_expect("mkdir repeat//dir",
                                   mr_fs_mkdir("repeat//dir"), 0);
    failures
        += fs_relative_expect("rename repeat//file",
                              fs_test_rename("repeat//file", "repeat//new"), 0);
    failures += fs_relative_stat_expect("/a/repeat/new", 0);
    failures += fs_relative_expect(
        "open missing/", mr_fs_open(&file, "missing/", MR_O_RDWR | MR_O_CREAT),
        -MR_EINVAL);
    failures
        += fs_relative_expect("mkdir dir/", mr_fs_mkdir("dir/"), -MR_EINVAL);
    failures += fs_relative_expect(
        "rename trailing /", fs_test_rename("/absnew", "move/"), -MR_EINVAL);
    failures += fs_relative_stat_expect("/absnew", 0);

    /* Restore context and unmount */
    failures += fs_relative_expect("chdir /", mr_fs_chdir("/"), 0);
    failures += fs_relative_expect("umount ramfs", mr_fs_umount("/"), 0);
    return failures;
}

static void relative(void) {
    MR_TEST_EXPECT_EQ_INT(fs_relative_test(), 0);
}
MR_TEST_EXPORT(fs, relative, relative);
#endif /* defined(MR_USE_TEST) && defined(MR_USE_FS) */
