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

static int fs_recursive_expect(const char *name, mr_err_t actual,
                               mr_err_t expected) {
    MR_IGNORE(name);

    MR_TEST_EXPECT_EQ_INT(actual, expected);
    return (actual == expected) ? 0 : 1;
}

static int fs_recursive_path_expect(const char *path, mr_err_t expected,
                                    mr_inode_type_t type) {
    mr_stat_t stat;
    mr_err_t ret;
    int failures;

    /* Get path status */
    ret = mr_fs_stat_by_path(path, &stat);
    failures = fs_recursive_expect(path, ret, expected);
    if ((ret == 0) && (stat.type != type)) {
        MR_TEST_EXPECT_EQ_INT(stat.type, type);
        failures++;
    }
    return failures;
}

static mr_err_t recursive_fs_inode_create(recursive_fs_node_t *node,
                                          mr_inode_type_t type,
                                          mr_inode_t **out) {
    mr_inode_t *inode;

    /* Create an inode for a backing node */
    inode = mr_inode_create(type, &__recursive_inode_ops,
                            &__recursive_file_ops, node);
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

static int fs_recursive_cached_test(void) {
    mr_file_t directory = MR_FILE_INIT(directory);
    mr_file_t base = MR_FILE_INIT(base);
    mr_file_t file = MR_FILE_INIT(file);
    mr_stat_t stat;
    int failures = 0;
    mr_err_t ret;

    /* Mount RAMFS and build a cached directory tree */
    ret = mr_fs_mount(MR_NULL, "/", "ramfs", 0, MR_NULL);
    failures += fs_recursive_expect("mount ramfs", ret, 0);
    if (ret != 0) {
        return failures;
    }
    failures += fs_recursive_expect("mkdir tree", mr_fs_mkdir("/tree"), 0);
    failures += fs_recursive_expect("mkdir one",
                                    mr_fs_mkdir("/tree/one"), 0);
    failures += fs_recursive_expect("mkdir two",
                                    mr_fs_mkdir("/tree/one/two"), 0);
    ret = mr_fs_open(&file, "/tree/one/two/file", MR_O_RDWR | MR_O_CREAT);
    failures += fs_recursive_expect("create nested file", ret, 0);
    if (ret == 0) {
        failures += fs_recursive_expect("close nested file",
                                        mr_fs_close(&file), 0);
    }
    ret = mr_fs_open(&file, "/outside", MR_O_RDWR | MR_O_CREAT);
    failures += fs_recursive_expect("create outside", ret, 0);
    if (ret == 0) {
        failures += fs_recursive_expect("close outside", mr_fs_close(&file),
                                        0);
    }
    failures += fs_recursive_expect(
        "create outside link", mr_fs_symlink("/outside", "/tree/link"), 0);

    /* A directory symbolic link is a leaf and is not followed */
    failures += fs_recursive_expect("mkdir keep-dir",
                                    mr_fs_mkdir("/keep-dir"), 0);
    ret = mr_fs_open(&file, "/keep-dir/file", MR_O_RDWR | MR_O_CREAT);
    failures += fs_recursive_expect("create keep-dir file", ret, 0);
    if (ret == 0) {
        failures += fs_recursive_expect("close keep-dir file",
                                        mr_fs_close(&file), 0);
    }
    failures += fs_recursive_expect(
        "create directory link",
        mr_fs_symlink("/keep-dir", "/tree/dirlink"), 0);
    failures += fs_recursive_expect(
        "unlink directory link",
        mr_fs_unlink_recursive_by_path("/tree/dirlink"), 0);
    failures += fs_recursive_path_expect("/tree/dirlink", -MR_ENOENT,
                                         MR_INODE_NONE);
    failures += fs_recursive_path_expect("/keep-dir/file", 0, MR_INODE_REG);

    /* Delete an open directory without following its symbolic link */
    ret = mr_fs_opendir(&directory, "/tree");
    failures += fs_recursive_expect("open recursive tree", ret, 0);
    if (ret == 0) {
        failures += fs_recursive_expect(
            "unlink recursive tree", mr_fs_unlink_recursive(&directory), 0);
        failures += fs_recursive_expect(
            "stat unlinked tree handle", mr_fs_stat(&directory, &stat), 0);
        failures += fs_recursive_expect(
            "close unlinked tree handle", mr_fs_closedir(&directory), 0);
    }
    failures += fs_recursive_path_expect("/tree", -MR_ENOENT, MR_INODE_NONE);
    failures += fs_recursive_path_expect("/outside", 0, MR_INODE_REG);

    /* Delete an open regular leaf through the same interface */
    ret = mr_fs_open(&file, "/outside", MR_O_RDONLY);
    failures += fs_recursive_expect("open recursive leaf", ret, 0);
    if (ret == 0) {
        failures += fs_recursive_expect(
            "unlink recursive leaf", mr_fs_unlink_recursive(&file), 0);
        failures += fs_recursive_expect(
            "stat unlinked leaf handle", mr_fs_stat(&file, &stat), 0);
        failures += fs_recursive_expect(
            "close unlinked leaf handle", mr_fs_close(&file), 0);
    }
    failures += fs_recursive_expect(
        "unlink recursive closed handle", mr_fs_unlink_recursive(&file),
        -MR_EBADF);
    failures += fs_recursive_path_expect("/outside", -MR_ENOENT,
                                         MR_INODE_NONE);

    /* Apply recursive deletion relative to a confined directory */
    failures += fs_recursive_expect("mkdir base", mr_fs_mkdir("/base"), 0);
    failures += fs_recursive_expect("mkdir confined",
                                    mr_fs_mkdir("/base/confined"), 0);
    failures += fs_recursive_expect("mkdir nested",
                                    mr_fs_mkdir("/base/confined/nested"), 0);
    failures += fs_recursive_expect("mkdir external",
                                    mr_fs_mkdir("/external"), 0);
    ret = mr_fs_opendir(&base, "/base");
    failures += fs_recursive_expect("open base", ret, 0);
    if (ret == 0) {
        failures += fs_recursive_expect(
            "protect confined root",
            mr_fs_unlinkat_recursive(&base, ".", MR_AT_IN_ROOT), -MR_EBUSY);
        failures += fs_recursive_path_expect("/base/confined", 0,
                                             MR_INODE_DIR);
        failures += fs_recursive_expect(
            "unlinkat parent clamp",
            mr_fs_unlinkat_recursive(&base, "../confined", MR_AT_IN_ROOT), 0);
        failures += fs_recursive_path_expect("/base/confined", -MR_ENOENT,
                                             MR_INODE_NONE);
        failures += fs_recursive_path_expect("/external", 0, MR_INODE_DIR);

        /* Delete a relative tree without confining the lookup root */
        failures += fs_recursive_expect("mkdir relative",
                                        mr_fs_mkdir("/base/rel"), 0);
        failures += fs_recursive_expect("mkdir relative nested",
                                        mr_fs_mkdir("/base/rel/nested"), 0);
        ret = mr_fs_open(&file, "/base/rel/nested/file",
                         MR_O_RDWR | MR_O_CREAT);
        failures += fs_recursive_expect("create relative file", ret, 0);
        if (ret == 0) {
            failures += fs_recursive_expect("close relative file",
                                            mr_fs_close(&file), 0);
        }
        failures += fs_recursive_expect(
            "unlinkat recursive relative",
            mr_fs_unlinkat_recursive(&base, "rel", 0), 0);
        failures += fs_recursive_path_expect("/base/rel", -MR_ENOENT,
                                             MR_INODE_NONE);

        /* Delete a leaf and an absolute path through the same API */
        ret = mr_fs_open(&file, "/base/leaf", MR_O_RDWR | MR_O_CREAT);
        failures += fs_recursive_expect("create leaf", ret, 0);
        if (ret == 0) {
            failures += fs_recursive_expect("close leaf", mr_fs_close(&file),
                                            0);
        }
        failures += fs_recursive_expect(
            "unlinkat recursive leaf",
            mr_fs_unlinkat_recursive(&base, "leaf", 0), 0);
        failures += fs_recursive_path_expect("/base/leaf", -MR_ENOENT,
                                             MR_INODE_NONE);
        ret = mr_fs_open(&file, "/abs-leaf", MR_O_RDWR | MR_O_CREAT);
        failures += fs_recursive_expect("create abs leaf", ret, 0);
        if (ret == 0) {
            failures += fs_recursive_expect("close abs leaf",
                                            mr_fs_close(&file), 0);
        }
        failures += fs_recursive_expect(
            "unlinkat recursive absolute",
            mr_fs_unlinkat_recursive(MR_NULL, "/abs-leaf", 0), 0);
        failures += fs_recursive_path_expect("/abs-leaf", -MR_ENOENT,
                                             MR_INODE_NONE);

        /* Remove a symbolic link without following its target */
        ret = mr_fs_open(&file, "/keep", MR_O_RDWR | MR_O_CREAT);
        failures += fs_recursive_expect("create keep", ret, 0);
        if (ret == 0) {
            failures += fs_recursive_expect("close keep", mr_fs_close(&file),
                                            0);
        }
        failures += fs_recursive_expect(
            "symlink keep", mr_fs_symlink("/keep", "/base/keep-link"), 0);
        failures += fs_recursive_expect(
            "unlinkat recursive symlink",
            mr_fs_unlinkat_recursive(&base, "keep-link", 0), 0);
        failures += fs_recursive_path_expect("/base/keep-link", -MR_ENOENT,
                                             MR_INODE_NONE);
        failures += fs_recursive_path_expect("/keep", 0, MR_INODE_REG);
        failures += fs_recursive_expect(
            "unlinkat recursive invalid flags",
            mr_fs_unlinkat_recursive(&base, ".", 0x10000), -MR_EINVAL);
        failures += fs_recursive_expect("close base", mr_fs_closedir(&base),
                                        0);
    }

    /* Stop at a nested mount without deleting its contents */
    failures += fs_recursive_expect("mkdir container",
                                    mr_fs_mkdir("/container"), 0);
    failures += fs_recursive_expect("mkdir mount point",
                                    mr_fs_mkdir("/container/mount"), 0);
    failures += fs_recursive_expect(
        "mount nested",
        mr_fs_mount(MR_NULL, "/container/mount", "ramfs", 0, MR_NULL), 0);
    ret = mr_fs_open(&file, "/container/mount/file", MR_O_RDWR | MR_O_CREAT);
    failures += fs_recursive_expect("create mounted file", ret, 0);
    if (ret == 0) {
        failures += fs_recursive_expect("close mounted file",
                                        mr_fs_close(&file), 0);
    }
    failures += fs_recursive_expect(
        "reject mount point",
        mr_fs_unlink_recursive_by_path("/container/mount"), -MR_EBUSY);
    failures += fs_recursive_path_expect("/container/mount/file", 0,
                                         MR_INODE_REG);
    failures += fs_recursive_expect(
        "reject nested mount",
        mr_fs_unlink_recursive_by_path("/container"),
        -MR_EBUSY);
    failures += fs_recursive_path_expect("/container/mount/file", 0,
                                         MR_INODE_REG);
    failures += fs_recursive_expect("umount nested",
                                    mr_fs_umount("/container/mount"), 0);
    failures += fs_recursive_expect(
        "unlink unmounted container",
        mr_fs_unlink_recursive_by_path("/container"), 0);

    /* Reject a missing path and the lookup root */
    failures += fs_recursive_expect(
        "unlink recursive missing",
        mr_fs_unlink_recursive_by_path("/missing"), -MR_ENOENT);
    failures += fs_recursive_expect(
        "unlink recursive root", mr_fs_unlink_recursive_by_path("/"),
        -MR_EBUSY);
    failures += fs_recursive_path_expect("/keep", 0, MR_INODE_REG);
    failures += fs_recursive_path_expect("/keep-dir/file", 0, MR_INODE_REG);

    /* Unmount RAMFS */
    failures += fs_recursive_expect("umount ramfs", mr_fs_umount("/"), 0);
    return failures;
}

static int fs_recursive_readdir_test(void) {
    recursive_fs_context_t context;
    int failures = 0;
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
    failures += fs_recursive_expect("register recursivefs", ret, 0);
    ret = mr_fs_mount(MR_NULL, "/", "recursivefs", 0, &context);
    failures += fs_recursive_expect("mount recursivefs", ret, 0);
    if (ret != 0) {
        return failures;
    }

    /* Discover through lookup/readdir and remove the backing tree */
    failures += fs_recursive_expect(
        "unlink backing tree", mr_fs_unlink_recursive_by_path("/dir"), 0);
    failures += fs_recursive_path_expect("/dir", -MR_ENOENT, MR_INODE_NONE);
    MR_TEST_EXPECT_EQ_INT(context.dir_exists, MR_FALSE);
    MR_TEST_EXPECT_EQ_INT(context.file_exists, MR_FALSE);
    MR_TEST_EXPECT_EQ_UINT(context.unlink_calls, 2);
    MR_TEST_EXPECT_EQ_INT(context.readdir_calls > 0, MR_TRUE);
    if (context.dir_exists || context.file_exists
        || (context.unlink_calls != 2) || (context.readdir_calls == 0)) {
        failures++;
    }

    /* Unmount the simulated file-system */
    failures += fs_recursive_expect("umount recursivefs", mr_fs_umount("/"),
                                    0);
    return failures;
}

static void recursive_cached(void) {
    MR_TEST_EXPECT_EQ_INT(fs_recursive_cached_test(), 0);
}
MR_TEST_EXPORT(fs, recursive_cached, recursive_cached);

static void recursive_readdir(void) {
    MR_TEST_EXPECT_EQ_INT(fs_recursive_readdir_test(), 0);
}
MR_TEST_EXPORT(fs, recursive_readdir, recursive_readdir);
#endif /* defined(MR_USE_TEST) && defined(MR_USE_FS) */
