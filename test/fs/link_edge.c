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

typedef struct link_edge_fs_context {
    mr_bool_t link_error;
    mr_bool_t symlink_error;
    mr_bool_t no_link;
    mr_size_t link_calls;
    mr_size_t symlink_calls;
} link_edge_fs_context_t;

static mr_err_t link_edge_inode_create(mr_inode_t *dir, const char *name,
                                       mr_inode_type_t type, mr_inode_t **out);
static mr_err_t link_edge_inode_link(mr_inode_t *dir, const char *name,
                                     mr_inode_t *inode);
static mr_err_t link_edge_inode_symlink(mr_inode_t *dir, const char *name,
                                        const char *target, mr_inode_t **out);
static mr_err_t link_edge_mount(mr_super_block_t *sb, const char *source,
                                int flags, void *data);

static const mr_inode_ops_t __link_edge_node_ops;
static const mr_inode_ops_t __link_edge_root_ops = {
    .create = link_edge_inode_create,
    .link = link_edge_inode_link,
    .symlink = link_edge_inode_symlink,
};
static const mr_inode_ops_t __link_edge_no_link_root_ops = {
    .create = link_edge_inode_create,
};

static mr_file_system_t __link_edge_fs
    = MR_FILE_SYSTEM_INIT("linkedgefs", link_edge_mount, MR_NULL, MR_NULL);

static void link_edge_expect(volatile mr_uint32_t *__test_status,
                             const char *name, mr_int64_t actual,
                             mr_int64_t expected) {
    MR_IGNORE(name);

    MR_TEST_EXPECT_EQ_INT(actual, expected);
}

static void link_edge_bool_expect(volatile mr_uint32_t *__test_status,
                                  const char *name, mr_bool_t actual,
                                  mr_bool_t expected) {
    MR_IGNORE(name);

    MR_TEST_EXPECT_EQ_INT(actual, expected);
}

static mr_err_t link_edge_inode_create(mr_inode_t *dir, const char *name,
                                       mr_inode_type_t type, mr_inode_t **out) {
    MR_IGNORE(dir);
    MR_IGNORE(name);

    /* Create inode */
    *out = mr_inode_create(type, &__link_edge_node_ops, MR_NULL, MR_NULL);
    return *out ? 0 : -MR_ENOMEM;
}

static mr_err_t link_edge_inode_link(mr_inode_t *dir, const char *name,
                                     mr_inode_t *inode) {
    link_edge_fs_context_t *context = dir->sb->priv;

    MR_IGNORE(name);
    MR_IGNORE(inode);

    /* Record callback */
    context->link_calls++;
    return context->link_error ? -MR_EIO : 0;
}

static mr_err_t link_edge_inode_symlink(mr_inode_t *dir, const char *name,
                                        const char *target, mr_inode_t **out) {
    link_edge_fs_context_t *context = dir->sb->priv;

    MR_IGNORE(name);
    MR_IGNORE(target);

    /* Record callback */
    context->symlink_calls++;
    if (context->symlink_error) {
        *out = MR_NULL;
        return -MR_EIO;
    }

    /* Create symbolic-link inode without readlink support */
    *out = mr_inode_create(MR_INODE_LNK, &__link_edge_node_ops, MR_NULL,
                           MR_NULL);
    return *out ? 0 : -MR_ENOMEM;
}

static mr_err_t link_edge_mount(mr_super_block_t *sb, const char *source,
                                int flags, void *data) {
    link_edge_fs_context_t *context = data;
    mr_inode_t *root;

    MR_IGNORE(source);
    MR_IGNORE(flags);

    /* Check mount data */
    if (!context) {
        return -MR_EINVAL;
    }

    /* Create root inode */
    root = mr_inode_create(MR_INODE_DIR,
                           context->no_link ? &__link_edge_no_link_root_ops
                                            : &__link_edge_root_ops,
                           MR_NULL, MR_NULL);
    if (!root) {
        return -MR_ENOMEM;
    }
    sb->priv = context;
    sb->root = root;
    return 0;
}

static void link_edge_ramfs_test(volatile mr_uint32_t *__test_status) {
    mr_file_t closed = MR_FILE_INIT(closed);
    mr_file_t source = MR_FILE_INIT(source);
    mr_file_t directory = MR_FILE_INIT(directory);
    mr_file_t hard = MR_FILE_INIT(hard);
    mr_file_t regular = MR_FILE_INIT(regular);
    mr_file_t symlink = MR_FILE_INIT(symlink);
    mr_stat_t stat;
    char buffer[16];
    mr_err_t ret;

    /* Mount RamFS */
    ret = fs_test_mount_root("ramfs", 0, MR_NULL);
    link_edge_expect(__test_status, "edge mount ramfs", ret, 0);
    if (ret != 0) {
        return;
    }

    /* Reject closed source handles */
    link_edge_expect(__test_status, "link closed source",
                     mr_fs_link(&closed, "/closed"), -MR_EBADF);
    link_edge_expect(__test_status, "readlink closed source",
                     mr_fs_readlink(&closed, buffer, sizeof(buffer)),
                     -MR_EBADF);

    /* Create the hard-link source */
    ret = mr_fs_open(&source, "/source", MR_O_RDWR | MR_O_CREAT);
    link_edge_expect(__test_status, "open edge source", ret, 0);
    if (ret == 0) {
        link_edge_expect(__test_status, "write edge source",
                         mr_fs_write(&source, "payload", 7), 7);
        link_edge_expect(__test_status, "create first hard link",
                         mr_fs_link(&source, "/hard-one"), 0);
        link_edge_expect(__test_status, "create second hard link",
                         mr_fs_link(&source, "/hard-two"), 0);
        link_edge_expect(__test_status, "reject duplicate hard link",
                         mr_fs_link(&source, "/hard-one"), -MR_EEXIST);
        link_edge_expect(__test_status, "reject missing hard-link parent",
                         mr_fs_link(&source, "/missing/hard"), -MR_ENOENT);
        link_edge_expect(__test_status, "stat source with three names",
                         mr_fs_stat(&source, &stat), 0);
        if (stat.nlink != 3) {
            MR_TEST_EXPECT_EQ_INT(stat.nlink, 3);
        }
    }

    /* Reject a regular-file parent and directory source */
    ret = mr_fs_open(&regular, "/regular", MR_O_RDONLY | MR_O_CREAT);
    link_edge_expect(__test_status, "create regular parent", ret, 0);
    if (ret == 0) {
        link_edge_expect(__test_status, "reject regular hard-link parent",
                         mr_fs_link(&source, "/regular/hard"), -MR_ENOTDIR);
        link_edge_expect(__test_status, "readlink regular file",
                         mr_fs_readlink(&regular, buffer, sizeof(buffer)),
                         -MR_EINVAL);
        link_edge_expect(__test_status, "close regular parent",
                         mr_fs_close(&regular), 0);
    }
    link_edge_expect(__test_status, "mkdir hard-link directory",
                     mr_fs_mkdir("/directory"), 0);
    ret = mr_fs_opendir(&directory, "/directory");
    link_edge_expect(__test_status, "open hard-link directory", ret, 0);
    if (ret == 0) {
        link_edge_expect(__test_status, "reject directory hard link",
                         mr_fs_link(&directory, "/directory-link"), -MR_EPERM);
        link_edge_expect(__test_status, "close hard-link directory",
                         mr_fs_closedir(&directory), 0);
    }

    /* Keep the inode alive while every hard-link name is removed */
    if (source.inode) {
        link_edge_expect(__test_status, "unlink source hard-link name",
                         mr_fs_unlink(&source), 0);
        link_edge_expect(__test_status, "stat unlinked source handle",
                         mr_fs_stat(&source, &stat), 0);
        if (stat.nlink != 2) {
            MR_TEST_EXPECT_EQ_INT(stat.nlink, 2);
        }
        link_edge_expect(__test_status, "link unlinked source handle",
                         mr_fs_link(&source, "/after-unlink"), 0);
        link_edge_expect(__test_status, "close unlinked source handle",
                         mr_fs_close(&source), 0);
    }
    link_edge_expect(__test_status, "source name absent",
                     fs_test_stat("/source", &stat), -MR_ENOENT);

    ret = mr_fs_open(&hard, "/hard-one", MR_O_RDONLY);
    link_edge_expect(__test_status, "open first remaining hard link", ret, 0);
    if (ret == 0) {
        link_edge_expect(__test_status, "stat three hard-link names",
                         mr_fs_stat(&hard, &stat), 0);
        if (stat.nlink != 3) {
            MR_TEST_EXPECT_EQ_INT(stat.nlink, 3);
        }
        link_edge_expect(__test_status, "unlink first remaining hard link",
                         mr_fs_unlink(&hard), 0);
        link_edge_expect(__test_status, "stat two hard-link names",
                         mr_fs_stat(&hard, &stat), 0);
        if (stat.nlink != 2) {
            MR_TEST_EXPECT_EQ_INT(stat.nlink, 2);
        }
        link_edge_expect(__test_status, "close first remaining hard link",
                         mr_fs_close(&hard), 0);
    }
    link_edge_expect(__test_status, "first hard-link name absent",
                     fs_test_stat("/hard-one", &stat), -MR_ENOENT);

    ret = mr_fs_open(&hard, "/hard-two", MR_O_RDONLY);
    link_edge_expect(__test_status, "open second remaining hard link", ret, 0);
    if (ret == 0) {
        link_edge_expect(__test_status, "read second remaining hard link",
                         mr_fs_read(&hard, buffer, sizeof(buffer)), 7);
        if (mr_memcmp(buffer, "payload", 7) != 0) {
            MR_TEST_EXPECT_EQ_INT(mr_memcmp(buffer, "payload", 7), 0);
        }
        link_edge_expect(__test_status, "unlink second remaining hard link",
                         mr_fs_unlink(&hard), 0);
        link_edge_expect(__test_status, "stat final hard-link name",
                         mr_fs_stat(&hard, &stat), 0);
        if (stat.nlink != 1) {
            MR_TEST_EXPECT_EQ_INT(stat.nlink, 1);
        }
        link_edge_expect(__test_status, "close second remaining hard link",
                         mr_fs_close(&hard), 0);
    }
    link_edge_expect(__test_status, "second hard-link name absent",
                     fs_test_stat("/hard-two", &stat), -MR_ENOENT);

    ret = mr_fs_open(&hard, "/after-unlink", MR_O_RDONLY);
    link_edge_expect(__test_status, "open final hard link", ret, 0);
    if (ret == 0) {
        link_edge_expect(__test_status, "read final hard link",
                         mr_fs_read(&hard, buffer, sizeof(buffer)), 7);
        if (mr_memcmp(buffer, "payload", 7) != 0) {
            MR_TEST_EXPECT_EQ_INT(mr_memcmp(buffer, "payload", 7), 0);
        }
        link_edge_expect(__test_status, "unlink final hard link",
                         mr_fs_unlink(&hard), 0);
        link_edge_expect(__test_status, "stat unlinked final hard link",
                         mr_fs_stat(&hard, &stat), 0);
        if (stat.nlink != 0) {
            MR_TEST_EXPECT_EQ_INT(stat.nlink, 0);
        }
        link_edge_expect(__test_status, "reject relink after final unlink",
                         mr_fs_link(&hard, "/revived-hard"), -MR_ENOENT);
        link_edge_expect(__test_status, "rejected relink path absent",
                         fs_test_stat("/revived-hard", &stat), -MR_ENOENT);
        link_edge_expect(__test_status, "read unlinked final hard link",
                         mr_fs_seek(&hard, 0, MR_SEEK_SET), 0);
        link_edge_expect(__test_status, "read unlinked hard-link data",
                         mr_fs_read(&hard, buffer, sizeof(buffer)), 7);
        link_edge_expect(__test_status, "close final hard link",
                         mr_fs_close(&hard), 0);
    }
    link_edge_expect(__test_status, "final hard-link name absent",
                     fs_test_stat("/after-unlink", &stat), -MR_ENOENT);

    /* Exercise symbolic-link errors and truncated reads */
    link_edge_expect(__test_status, "create dangling symbolic link",
                     mr_fs_symlink("missing-target", "/dangling"), 0);
    link_edge_expect(__test_status, "reject duplicate symbolic link",
                     mr_fs_symlink("other", "/dangling"), -MR_EEXIST);
    link_edge_expect(__test_status, "reject missing symbolic-link parent",
                     mr_fs_symlink("other", "/missing/link"), -MR_ENOENT);
    link_edge_expect(__test_status, "reject regular symbolic-link parent",
                     mr_fs_symlink("other", "/regular/link"), -MR_ENOTDIR);
    link_edge_expect(__test_status, "follow dangling symbolic link",
                     mr_fs_open(&symlink, "/dangling", MR_O_RDONLY),
                     -MR_ENOENT);

    ret = mr_fs_open(&symlink, "/dangling", MR_O_RDONLY | MR_O_NOFOLLOW);
    link_edge_expect(__test_status, "open dangling symbolic link", ret, 0);
    if (ret == 0) {
        link_edge_expect(__test_status, "readlink zero-sized buffer",
                         mr_fs_readlink(&symlink, MR_NULL, 0), 0);
        mr_memset(buffer, 0, sizeof(buffer));
        link_edge_expect(__test_status, "readlink truncated target",
                         mr_fs_readlink(&symlink, buffer, 4), 4);
        if (mr_memcmp(buffer, "miss", 4) != 0) {
            MR_TEST_EXPECT_EQ_INT(mr_memcmp(buffer, "miss", 4), 0);
        }
        link_edge_expect(__test_status, "unlink open symbolic link",
                         mr_fs_unlink(&symlink), 0);
        link_edge_expect(__test_status, "readlink unlinked symbolic link",
                         mr_fs_readlink(&symlink, buffer, sizeof(buffer)), 14);
        if (mr_memcmp(buffer, "missing-target", 14) != 0) {
            MR_TEST_EXPECT_EQ_INT(mr_memcmp(buffer, "missing-target", 14), 0);
        }
        link_edge_expect(__test_status, "stat unlinked symbolic link",
                         mr_fs_stat(&symlink, &stat), 0);
        if (stat.nlink != 0) {
            MR_TEST_EXPECT_EQ_INT(stat.nlink, 0);
        }
        link_edge_expect(__test_status, "repeat unlink symbolic link",
                         mr_fs_unlink(&symlink), -MR_ENOENT);
        link_edge_expect(__test_status, "close unlinked symbolic link",
                         mr_fs_close(&symlink), 0);
    }

    /* Unmount RamFS */
    link_edge_expect(__test_status, "edge umount ramfs", mr_fs_umount("/"), 0);
    return;
}

static void link_edge_callback_test(volatile mr_uint32_t *__test_status) {
    link_edge_fs_context_t context = {0};
    mr_file_t source = MR_FILE_INIT(source);
    mr_file_t symlink = MR_FILE_INIT(symlink);
    mr_stat_t stat;
    char buffer[1];
    mr_err_t ret;

    /* Register callback test filesystem */
    ret = mr_file_system_register(&__link_edge_fs);
    link_edge_expect(__test_status, "register link callback fs", ret, 0);
    if (ret != 0) {
        return;
    }

    /* Mount callback test filesystem */
    ret = fs_test_mount_root("linkedgefs", 0, &context);
    link_edge_expect(__test_status, "mount link callback fs", ret, 0);
    if (ret != 0) {
        return;
    }

    /* Roll back hard-link state after a callback failure */
    ret = mr_fs_open(&source, "/source", MR_O_RDONLY | MR_O_CREAT);
    link_edge_expect(__test_status, "open callback source", ret, 0);
    if (ret == 0) {
        context.link_error = MR_TRUE;
        link_edge_expect(__test_status, "propagate hard-link callback error",
                         mr_fs_link(&source, "/failed-hard"), -MR_EIO);
        context.link_error = MR_FALSE;
        link_edge_bool_expect(__test_status, "hard-link callback called",
                              context.link_calls == 1, MR_TRUE);
        link_edge_expect(__test_status, "stat failed hard-link source",
                         mr_fs_stat(&source, &stat), 0);
        if (stat.nlink != 1) {
            MR_TEST_EXPECT_EQ_INT(stat.nlink, 1);
        }
        link_edge_expect(__test_status, "failed hard-link path absent",
                         fs_test_stat("/failed-hard", &stat), -MR_ENOENT);
        link_edge_expect(__test_status, "close callback source",
                         mr_fs_close(&source), 0);
    }

    /* Roll back failed symbolic-link creation */
    context.symlink_error = MR_TRUE;
    link_edge_expect(__test_status, "propagate symbolic-link callback error",
                     mr_fs_symlink("target", "/failed-symbolic"), -MR_EIO);
    context.symlink_error = MR_FALSE;
    link_edge_bool_expect(__test_status, "symbolic-link callback called",
                          context.symlink_calls == 1, MR_TRUE);
    link_edge_expect(__test_status, "failed symbolic-link path absent",
                     fs_test_stat("/failed-symbolic", &stat), -MR_ENOENT);

    /* Report a missing readlink callback */
    link_edge_expect(__test_status, "create callback symbolic link",
                     mr_fs_symlink("target", "/no-readlink"), 0);
    ret = mr_fs_open(&symlink, "/no-readlink", MR_O_RDONLY | MR_O_NOFOLLOW);
    link_edge_expect(__test_status, "open callback symbolic link", ret, 0);
    if (ret == 0) {
        link_edge_expect(__test_status, "reject missing readlink callback",
                         mr_fs_readlink(&symlink, buffer, sizeof(buffer)),
                         -MR_ENOSYS);
        link_edge_expect(__test_status, "close callback symbolic link",
                         mr_fs_close(&symlink), 0);
    }
    link_edge_expect(__test_status, "follow missing readlink",
                     mr_fs_open(&symlink, "/no-readlink", MR_O_RDONLY),
                     -MR_ENOSYS);

    /* Unmount callback test filesystem */
    link_edge_expect(__test_status, "umount link callback fs",
                     mr_fs_umount("/"), 0);

    /* A missing link callback uses the built-in namespace operation */
    context.no_link = MR_TRUE;
    ret = fs_test_mount_root("linkedgefs", 0, &context);
    link_edge_expect(__test_status, "mount no-link callback fs", ret, 0);
    if (ret == 0) {
        ret = mr_fs_open(&source, "/unsupported", MR_O_RDONLY | MR_O_CREAT);
        link_edge_expect(__test_status, "open unsupported link source", ret, 0);
        if (ret == 0) {
            link_edge_expect(__test_status, "use built-in hard-link operation",
                             mr_fs_link(&source, "/unsupported-link"), 0);
            link_edge_expect(__test_status, "stat built-in hard link",
                             mr_fs_stat_by_path("/unsupported-link", &stat), 0);
            link_edge_expect(__test_status, "unlink built-in hard link",
                             mr_fs_unlink_by_path("/unsupported-link"), 0);
            link_edge_expect(__test_status, "close unsupported link source",
                             mr_fs_close(&source), 0);
        }
        link_edge_expect(__test_status, "use built-in symbolic-link operation",
                         mr_fs_symlink("x", "/builtin-symbolic"), 0);
        ret = mr_fs_open(&symlink, "/builtin-symbolic",
                         MR_O_RDONLY | MR_O_NOFOLLOW);
        link_edge_expect(__test_status, "open built-in symbolic link", ret, 0);
        if (ret == 0) {
            link_edge_expect(__test_status, "read built-in symbolic link",
                             mr_fs_readlink(&symlink, buffer, sizeof(buffer)),
                             1);
            link_edge_expect(__test_status, "built-in symbolic target",
                             buffer[0], 'x');
            link_edge_expect(__test_status, "close built-in symbolic link",
                             mr_fs_close(&symlink), 0);
        }
        link_edge_expect(__test_status, "unlink built-in symbolic link",
                         mr_fs_unlink_by_path("/builtin-symbolic"), 0);
        link_edge_expect(__test_status, "umount no-link callback fs",
                         mr_fs_umount("/"), 0);
    }
    return;
}

MR_TEST(fs, link_edge) {
    /* Run link edge tests */
    link_edge_ramfs_test(__test_status);
    link_edge_callback_test(__test_status);
}

#endif /* defined(MR_USE_TEST) && defined(MR_USE_FS) */
