/**
 * @copyright (c) 2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2026-08-15        MacRsh        First version
 */

#include <mrX/test/mr_test.h>
#if defined(MR_USE_TEST) && defined(MR_USE_FS)
#include <mrX.h>
#include "fs_test.h"

typedef enum recursive_node_id {
    RECURSIVE_NODE_ROOT,
    RECURSIVE_NODE_DIR,
    RECURSIVE_NODE_FILE,
} recursive_node_id_t;

typedef struct recursive_fs_context recursive_fs_context_t;

typedef struct recursive_fs_node {
    recursive_fs_context_t *context;
    recursive_node_id_t id;
} recursive_fs_node_t;

struct recursive_fs_context {
    recursive_fs_node_t root;
    recursive_fs_node_t dir;
    recursive_fs_node_t file;
    mr_size_t readdir_calls;
    mr_size_t unlink_calls;
    mr_bool_t dir_exists;
    mr_bool_t file_exists;
};

static mr_err_t recursive_fs_lookup(mr_inode_t *dir, const char *name,
                                    mr_inode_t **out);
static mr_err_t recursive_fs_unlink(mr_inode_t *dir, const char *name);
static mr_err_t recursive_fs_readdir(mr_file_t *file, mr_dirent_t *dirent);
static mr_err_t recursive_fs_mount(mr_super_block_t *sb, const char *source,
                                   int flags, void *data);

static const mr_inode_ops_t __recursive_inode_ops = {
    .lookup = recursive_fs_lookup,
    .unlink = recursive_fs_unlink,
};

static const mr_file_ops_t __recursive_file_ops = {
    .readdir = recursive_fs_readdir,
};

static mr_file_system_t __recursive_fs
    = MR_FILE_SYSTEM_INIT("recursivefs", recursive_fs_mount, MR_NULL, MR_NULL);

static void fs_recursive_expect(volatile mr_uint32_t *__test_status,
                                const char *name, mr_err_t actual,
                                mr_err_t expected) {
    MR_IGNORE(name);

    MR_TEST_EXPECT_EQ_INT(actual, expected);
}

static void fs_recursive_path_expect(volatile mr_uint32_t *__test_status,
                                     const char *path, mr_err_t expected,
                                     mr_inode_type_t type) {
    mr_stat_t stat;
    mr_err_t ret;

    /* Get path status */
    ret = mr_fs_stat_by_path(path, &stat);
    fs_recursive_expect(__test_status, path, ret, expected);
    if ((ret == 0) && (stat.type != type)) {
        MR_TEST_EXPECT_EQ_INT(stat.type, type);
    }
    return;
}

static mr_err_t recursive_fs_inode_create(recursive_fs_node_t *node,
                                          mr_inode_type_t type,
                                          mr_inode_t **out) {
    mr_inode_t *inode;

    /* Create an inode for a backing node */
    inode = mr_inode_create(type, &__recursive_inode_ops, &__recursive_file_ops,
                            node);
    if (!inode) {
        *out = MR_NULL;
        return -MR_ENOMEM;
    }
    *out = inode;
    return 0;
}

static mr_err_t recursive_fs_lookup(mr_inode_t *dir, const char *name,
                                    mr_inode_t **out) {
    recursive_fs_node_t *node = dir->priv;
    recursive_fs_context_t *context = node->context;

    /* Resolve the simulated backing directory */
    *out = MR_NULL;
    if ((node->id == RECURSIVE_NODE_ROOT) && context->dir_exists
        && (mr_strcmp(name, "dir") == 0)) {
        return recursive_fs_inode_create(&context->dir, MR_INODE_DIR, out);
    }
    if ((node->id == RECURSIVE_NODE_DIR) && context->file_exists
        && (mr_strcmp(name, "file") == 0)) {
        return recursive_fs_inode_create(&context->file, MR_INODE_REG, out);
    }
    return -MR_ENOENT;
}

static mr_err_t recursive_fs_unlink(mr_inode_t *dir, const char *name) {
    recursive_fs_node_t *node = dir->priv;
    recursive_fs_context_t *context = node->context;

    /* Remove a simulated backing entry */
    if ((node->id == RECURSIVE_NODE_DIR) && context->file_exists
        && (mr_strcmp(name, "file") == 0)) {
        context->file_exists = MR_FALSE;
        context->unlink_calls++;
        return 0;
    }
    if ((node->id == RECURSIVE_NODE_ROOT) && context->dir_exists
        && (mr_strcmp(name, "dir") == 0)) {
        if (context->file_exists) {
            return -MR_ENOTEMPTY;
        }
        context->dir_exists = MR_FALSE;
        context->unlink_calls++;
        return 0;
    }
    return -MR_ENOENT;
}

static mr_err_t recursive_fs_readdir(mr_file_t *file, mr_dirent_t *dirent) {
    recursive_fs_node_t *node = file->inode->priv;
    recursive_fs_context_t *context = node->context;

    /* Enumerate the uncached backing child */
    context->readdir_calls++;
    if ((node->id != RECURSIVE_NODE_DIR) || (!context->file_exists)
        || (file->pos != 0)) {
        return 0;
    }
    mr_strncpy(dirent->name, "file", sizeof(dirent->name));
    dirent->type = MR_INODE_REG;
    file->pos++;
    return 1;
}

static mr_err_t recursive_fs_mount(mr_super_block_t *sb, const char *source,
                                   int flags, void *data) {
    recursive_fs_context_t *context = data;
    mr_inode_t *root;

    MR_IGNORE(source);
    MR_IGNORE(flags);

    /* Create the simulated backing root */
    root = mr_inode_create(MR_INODE_DIR, &__recursive_inode_ops,
                           &__recursive_file_ops, &context->root);
    if (!root) {
        return -MR_ENOMEM;
    }
    sb->root = root;
    sb->priv = context;
    return 0;
}

static void fs_recursive_cached_test(volatile mr_uint32_t *__test_status) {
    mr_file_t directory = MR_FILE_INIT(directory);
    mr_file_t base = MR_FILE_INIT(base);
    mr_file_t file = MR_FILE_INIT(file);
    mr_stat_t stat;
    mr_err_t ret;

    /* Mount RAMFS and build a cached directory tree */
    ret = fs_test_mount_root("ramfs", 0, MR_NULL);
    fs_recursive_expect(__test_status, "mount ramfs", ret, 0);
    if (ret != 0) {
        return;
    }
    fs_recursive_expect(__test_status, "mkdir tree", mr_fs_mkdir("/tree"), 0);
    fs_recursive_expect(__test_status, "mkdir one", mr_fs_mkdir("/tree/one"),
                        0);
    fs_recursive_expect(__test_status, "mkdir two",
                        mr_fs_mkdir("/tree/one/two"), 0);
    ret = mr_fs_open(&file, "/tree/one/two/file", MR_O_RDWR | MR_O_CREAT);
    fs_recursive_expect(__test_status, "create nested file", ret, 0);
    if (ret == 0) {
        fs_recursive_expect(__test_status, "close nested file",
                            mr_fs_close(&file), 0);
    }
    ret = mr_fs_open(&file, "/outside", MR_O_RDWR | MR_O_CREAT);
    fs_recursive_expect(__test_status, "create outside", ret, 0);
    if (ret == 0) {
        fs_recursive_expect(__test_status, "close outside", mr_fs_close(&file),
                            0);
    }
    fs_recursive_expect(__test_status, "create outside link",
                        mr_fs_symlink("/outside", "/tree/link"), 0);

    /* A directory symbolic link is a leaf and is not followed */
    fs_recursive_expect(__test_status, "mkdir keep-dir",
                        mr_fs_mkdir("/keep-dir"), 0);
    ret = mr_fs_open(&file, "/keep-dir/file", MR_O_RDWR | MR_O_CREAT);
    fs_recursive_expect(__test_status, "create keep-dir file", ret, 0);
    if (ret == 0) {
        fs_recursive_expect(__test_status, "close keep-dir file",
                            mr_fs_close(&file), 0);
    }
    fs_recursive_expect(__test_status, "create directory link",
                        mr_fs_symlink("/keep-dir", "/tree/dirlink"), 0);
    fs_recursive_expect(__test_status, "unlink directory link",
                        mr_fs_unlink_recursive_by_path("/tree/dirlink"), 0);
    fs_recursive_path_expect(__test_status, "/tree/dirlink", -MR_ENOENT,
                             MR_INODE_NONE);
    fs_recursive_path_expect(__test_status, "/keep-dir/file", 0, MR_INODE_REG);

    /* Delete an open directory without following its symbolic link */
    ret = mr_fs_opendir(&directory, "/tree");
    fs_recursive_expect(__test_status, "open recursive tree", ret, 0);
    if (ret == 0) {
        fs_recursive_expect(__test_status, "unlink recursive tree",
                            mr_fs_unlink_recursive(&directory), 0);
        fs_recursive_expect(__test_status, "stat unlinked tree handle",
                            mr_fs_stat(&directory, &stat), 0);
        fs_recursive_expect(__test_status, "close unlinked tree handle",
                            mr_fs_closedir(&directory), 0);
    }
    fs_recursive_path_expect(__test_status, "/tree", -MR_ENOENT, MR_INODE_NONE);
    fs_recursive_path_expect(__test_status, "/outside", 0, MR_INODE_REG);

    /* Delete an open regular leaf through the same interface */
    ret = mr_fs_open(&file, "/outside", MR_O_RDONLY);
    fs_recursive_expect(__test_status, "open recursive leaf", ret, 0);
    if (ret == 0) {
        fs_recursive_expect(__test_status, "unlink recursive leaf",
                            mr_fs_unlink_recursive(&file), 0);
        fs_recursive_expect(__test_status, "stat unlinked leaf handle",
                            mr_fs_stat(&file, &stat), 0);
        fs_recursive_expect(__test_status, "close unlinked leaf handle",
                            mr_fs_close(&file), 0);
    }
    fs_recursive_expect(__test_status, "unlink recursive closed handle",
                        mr_fs_unlink_recursive(&file), -MR_EBADF);
    fs_recursive_path_expect(__test_status, "/outside", -MR_ENOENT,
                             MR_INODE_NONE);

    /* Apply recursive deletion relative to a confined directory */
    fs_recursive_expect(__test_status, "mkdir base", mr_fs_mkdir("/base"), 0);
    fs_recursive_expect(__test_status, "mkdir confined",
                        mr_fs_mkdir("/base/confined"), 0);
    fs_recursive_expect(__test_status, "mkdir nested",
                        mr_fs_mkdir("/base/confined/nested"), 0);
    fs_recursive_expect(__test_status, "mkdir external",
                        mr_fs_mkdir("/external"), 0);
    ret = mr_fs_opendir(&base, "/base");
    fs_recursive_expect(__test_status, "open base", ret, 0);
    if (ret == 0) {
        fs_recursive_expect(
            __test_status, "protect confined root",
            mr_fs_unlinkat_recursive_by_path(&base, ".", MR_AT_IN_ROOT),
            -MR_EBUSY);
        fs_recursive_path_expect(__test_status, "/base/confined", 0,
                                 MR_INODE_DIR);
        fs_recursive_expect(__test_status, "unlinkat parent clamp",
                            mr_fs_unlinkat_recursive_by_path(
                                &base, "../confined", MR_AT_IN_ROOT),
                            0);
        fs_recursive_path_expect(__test_status, "/base/confined", -MR_ENOENT,
                                 MR_INODE_NONE);
        fs_recursive_path_expect(__test_status, "/external", 0, MR_INODE_DIR);

        /* Delete a relative tree without confining the lookup root */
        fs_recursive_expect(__test_status, "mkdir relative",
                            mr_fs_mkdir("/base/rel"), 0);
        fs_recursive_expect(__test_status, "mkdir relative nested",
                            mr_fs_mkdir("/base/rel/nested"), 0);
        ret = mr_fs_open(&file, "/base/rel/nested/file",
                         MR_O_RDWR | MR_O_CREAT);
        fs_recursive_expect(__test_status, "create relative file", ret, 0);
        if (ret == 0) {
            fs_recursive_expect(__test_status, "close relative file",
                                mr_fs_close(&file), 0);
        }
        fs_recursive_expect(__test_status, "unlinkat recursive relative",
                            mr_fs_unlinkat_recursive_by_path(&base, "rel", 0),
                            0);
        fs_recursive_path_expect(__test_status, "/base/rel", -MR_ENOENT,
                                 MR_INODE_NONE);

        /* Delete a leaf and an absolute path through the same API */
        ret = mr_fs_open(&file, "/base/leaf", MR_O_RDWR | MR_O_CREAT);
        fs_recursive_expect(__test_status, "create leaf", ret, 0);
        if (ret == 0) {
            fs_recursive_expect(__test_status, "close leaf", mr_fs_close(&file),
                                0);
        }
        fs_recursive_expect(__test_status, "unlinkat recursive leaf",
                            mr_fs_unlinkat_recursive_by_path(&base, "leaf", 0),
                            0);
        fs_recursive_path_expect(__test_status, "/base/leaf", -MR_ENOENT,
                                 MR_INODE_NONE);
        ret = mr_fs_open(&file, "/abs-leaf", MR_O_RDWR | MR_O_CREAT);
        fs_recursive_expect(__test_status, "create abs leaf", ret, 0);
        if (ret == 0) {
            fs_recursive_expect(__test_status, "close abs leaf",
                                mr_fs_close(&file), 0);
        }
        fs_recursive_expect(
            __test_status, "unlinkat recursive absolute",
            mr_fs_unlinkat_recursive_by_path(MR_NULL, "/abs-leaf", 0), 0);
        fs_recursive_path_expect(__test_status, "/abs-leaf", -MR_ENOENT,
                                 MR_INODE_NONE);

        /* Remove a symbolic link without following its target */
        ret = mr_fs_open(&file, "/keep", MR_O_RDWR | MR_O_CREAT);
        fs_recursive_expect(__test_status, "create keep", ret, 0);
        if (ret == 0) {
            fs_recursive_expect(__test_status, "close keep", mr_fs_close(&file),
                                0);
        }
        fs_recursive_expect(__test_status, "symlink keep",
                            mr_fs_symlink("/keep", "/base/keep-link"), 0);
        fs_recursive_expect(
            __test_status, "unlinkat recursive symlink",
            mr_fs_unlinkat_recursive_by_path(&base, "keep-link", 0), 0);
        fs_recursive_path_expect(__test_status, "/base/keep-link", -MR_ENOENT,
                                 MR_INODE_NONE);
        fs_recursive_path_expect(__test_status, "/keep", 0, MR_INODE_REG);
        fs_recursive_expect(
            __test_status, "unlinkat recursive invalid flags",
            mr_fs_unlinkat_recursive_by_path(&base, ".", 0x10000), -MR_EINVAL);
        fs_recursive_expect(__test_status, "close base", mr_fs_closedir(&base),
                            0);
    }

    /* Stop at a nested mount without deleting its contents */
    fs_recursive_expect(__test_status, "mkdir container",
                        mr_fs_mkdir("/container"), 0);
    fs_recursive_expect(__test_status, "mkdir mount point",
                        mr_fs_mkdir("/container/mount"), 0);
    fs_recursive_expect(
        __test_status, "mount nested",
        mr_fs_mount(MR_NULL, "/container/mount", "ramfs", 0, MR_NULL), 0);
    ret = mr_fs_open(&file, "/container/mount/file", MR_O_RDWR | MR_O_CREAT);
    fs_recursive_expect(__test_status, "create mounted file", ret, 0);
    if (ret == 0) {
        fs_recursive_expect(__test_status, "close mounted file",
                            mr_fs_close(&file), 0);
    }
    fs_recursive_expect(__test_status, "reject mount point",
                        mr_fs_unlink_recursive_by_path("/container/mount"),
                        -MR_EBUSY);
    fs_recursive_path_expect(__test_status, "/container/mount/file", 0,
                             MR_INODE_REG);
    fs_recursive_expect(__test_status, "reject nested mount",
                        mr_fs_unlink_recursive_by_path("/container"),
                        -MR_EBUSY);
    fs_recursive_path_expect(__test_status, "/container/mount/file", 0,
                             MR_INODE_REG);
    fs_recursive_expect(__test_status, "umount nested",
                        mr_fs_umount("/container/mount"), 0);
    fs_recursive_expect(__test_status, "unlink unmounted container",
                        mr_fs_unlink_recursive_by_path("/container"), 0);

    /* Reject a missing path and the lookup root */
    fs_recursive_expect(__test_status, "unlink recursive missing",
                        mr_fs_unlink_recursive_by_path("/missing"), -MR_ENOENT);
    fs_recursive_expect(__test_status, "unlink recursive root",
                        mr_fs_unlink_recursive_by_path("/"), -MR_EBUSY);
    fs_recursive_path_expect(__test_status, "/keep", 0, MR_INODE_REG);
    fs_recursive_path_expect(__test_status, "/keep-dir/file", 0, MR_INODE_REG);

    /* Unmount RAMFS */
    fs_recursive_expect(__test_status, "umount ramfs", mr_fs_umount("/"), 0);
    return;
}

static void fs_recursive_readdir_test(volatile mr_uint32_t *__test_status) {
    recursive_fs_context_t context;
    mr_err_t ret;

    /* Init simulated backing tree */
    mr_memset(&context, 0, sizeof(context));
    context.root.context = &context;
    context.root.id = RECURSIVE_NODE_ROOT;
    context.dir.context = &context;
    context.dir.id = RECURSIVE_NODE_DIR;
    context.file.context = &context;
    context.file.id = RECURSIVE_NODE_FILE;
    context.dir_exists = MR_TRUE;
    context.file_exists = MR_TRUE;

    /* Register and mount the simulated external file-system */
    ret = mr_file_system_register(&__recursive_fs);
    fs_recursive_expect(__test_status, "register recursivefs", ret, 0);
    ret = fs_test_mount_root("recursivefs", 0, &context);
    fs_recursive_expect(__test_status, "mount recursivefs", ret, 0);
    if (ret != 0) {
        return;
    }

    /* Discover through lookup/readdir and remove the backing tree */
    fs_recursive_expect(__test_status, "unlink backing tree",
                        mr_fs_unlink_recursive_by_path("/dir"), 0);
    fs_recursive_path_expect(__test_status, "/dir", -MR_ENOENT, MR_INODE_NONE);
    MR_TEST_EXPECT_EQ_INT(context.dir_exists, MR_FALSE);
    MR_TEST_EXPECT_EQ_INT(context.file_exists, MR_FALSE);
    MR_TEST_EXPECT_EQ_UINT(context.unlink_calls, 2);
    MR_TEST_EXPECT_EQ_INT(context.readdir_calls > 0, MR_TRUE);
    if (context.dir_exists || context.file_exists || (context.unlink_calls != 2)
        || (context.readdir_calls == 0)) {
    }

    /* Unmount the simulated file-system */
    fs_recursive_expect(__test_status, "umount recursivefs", mr_fs_umount("/"),
                        0);
    return;
}

MR_TEST(fs, recursive_cached) {
    fs_recursive_cached_test(__test_status);
}

MR_TEST(fs, recursive_readdir) {
    fs_recursive_readdir_test(__test_status);
}

#endif /* defined(MR_USE_TEST) && defined(MR_USE_FS) */
