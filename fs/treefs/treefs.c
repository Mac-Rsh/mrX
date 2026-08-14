/**
 * @copyright (c) 2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2026-07-20        MacRsh        First version
 */

#include <mrX/fs/mr_file.h>
#if defined(MR_USE_FS_TREEFS)

/* FS name */
#define FS_NAME                         "treefs"

/* TreeFS inode operations */
static mr_err_t treefs_inode_create(mr_inode_t *dir, const char *name,
                                    mr_inode_type_t type, mr_inode_t **out);
static mr_err_t treefs_inode_mknod(mr_inode_t *dir, const char *name,
                                   mr_inode_type_t type, void *priv,
                                   mr_inode_t **out);
static const mr_inode_ops_t __treefs_inodes = {
    .create = treefs_inode_create,
    .mknod = treefs_inode_mknod,
};

/* TreeFS file system operations */
static mr_err_t treefs_mount(mr_super_block_t *sb, const char *source,
                             int flags, void *data);
static mr_file_system_t __treefs
    = MR_FILE_SYSTEM_INIT(FS_NAME, treefs_mount, MR_NULL, MR_NULL);

static mr_err_t treefs_inode_alloc(mr_inode_type_t type, void *priv,
                                   mr_inode_t **out) {
    mr_inode_t *inode;

    /* Create a namespace-only inode */
    inode = mr_inode_create(type, &__treefs_inodes, MR_NULL, priv);
    if (!inode) {
        *out = MR_NULL;
        return -MR_ENOMEM;
    }
    *out = inode;
    return 0;
}

static mr_err_t treefs_inode_create(mr_inode_t *dir, const char *name,
                                    mr_inode_type_t type, mr_inode_t **out) {
    MR_IGNORE(dir);
    MR_IGNORE(name);

    /* Generic creation is reserved for directories */
    if (type != MR_INODE_DIR) {
        *out = MR_NULL;
        return -MR_ENOSYS;
    }
    return treefs_inode_alloc(type, MR_NULL, out);
}

static mr_err_t treefs_inode_mknod(mr_inode_t *dir, const char *name,
                                   mr_inode_type_t type, void *priv,
                                   mr_inode_t **out) {
    MR_IGNORE(dir);
    MR_IGNORE(name);

    /* The private pointer is borrowed for the inode lifetime */
    return treefs_inode_alloc(type, priv, out);
}

static mr_err_t treefs_mount(mr_super_block_t *sb, const char *source,
                             int flags, void *data) {
    mr_inode_t *root;

    MR_IGNORE(source);
    MR_IGNORE(flags);
    MR_IGNORE(data);

    /* Create root inode */
    root = mr_inode_create(MR_INODE_DIR, &__treefs_inodes, MR_NULL, MR_NULL);
    if (!root) {
        return -MR_ENOMEM;
    }
    sb->root = root;
    sb->dops = MR_NULL;
    return 0;
}

static void mr_fs_treefs_init(void) {
    mr_file_system_register(&__treefs);
}
MR_INIT_EXPORT(mr_fs_treefs_init, MR_INIT_LEVEL_FS);
#endif /* defined(MR_USE_FS_TREEFS) */
