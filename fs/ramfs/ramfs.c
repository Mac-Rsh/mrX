/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/fs/mr_file.h>
#if defined(MR_USE_FS_RAMFS)
#include <mrX/libc/mr_malloc.h>
#include <mrX/libc/mr_string.h>

#define RAMFS_NAME                      "ramfs"

/* RamFs inode operations */
static mr_err_t ramfs_inode_create(mr_inode_t *dir, const char *name,
                                   mr_inode_type_t type, mr_inode_t **out);
static mr_err_t ramfs_inode_stat(mr_inode_t *inode, mr_stat_t *stat);
static mr_err_t ramfs_inode_unlink(mr_inode_t *dir, const char *name);
static mr_err_t ramfs_inode_rename(mr_inode_t *old_dir, const char *old_name,
                                   mr_inode_t *new_dir, const char *new_name);
static mr_err_t ramfs_inode_truncate(mr_inode_t *inode, mr_int64_t length);
static void ramfs_inode_destroy(mr_inode_t *inode);
static const mr_inode_ops_t __ramfs_inodes = {
    .create = ramfs_inode_create,
    .stat = ramfs_inode_stat,
    .unlink = ramfs_inode_unlink,
    .rename = ramfs_inode_rename,
    .truncate = ramfs_inode_truncate,
    .destroy = ramfs_inode_destroy,
};

/* RamFs file operations */
static mr_err_t ramfs_file_open(mr_inode_t *inode, mr_file_t *file);
static mr_err_t ramfs_file_close(mr_file_t *file);
static mr_ssize_t ramfs_file_read(mr_file_t *file, void *buf, mr_size_t size);
static mr_ssize_t ramfs_file_write(mr_file_t *file, const void *buf,
                                   mr_size_t size);
static mr_bool_t ramfs_file_readdir(mr_file_t *file, mr_dirent_t *dirent);
static const mr_file_ops_t __ramfs_files = {
    .open = ramfs_file_open,
    .close = ramfs_file_close,
    .read = ramfs_file_read,
    .write = ramfs_file_write,
    .readdir = ramfs_file_readdir,
};

/* RamFs file system operations */
static mr_err_t ramfs_mount(mr_super_block_t *sb, const char *source, int flags,
                            void *data);
static void ramfs_kill_sb(mr_super_block_t *sb);
static mr_file_system_t __ramfs
    = MR_FILE_SYSTEM_INIT(RAMFS_NAME, ramfs_mount, ramfs_kill_sb);

/* RamFs node type */
typedef struct ramfs_node {
    mr_uint8_t *data;
} ramfs_node_t;

static mr_err_t ramfs_inode_create(mr_inode_t *dir, const char *name,
                                   mr_inode_type_t type, mr_inode_t **out) {
    ramfs_node_t *node;
    mr_inode_t *inode;
    mr_err_t ret;

    MR_IGNORE(dir);
    MR_IGNORE(name);

    /* Create ramfs node */
    node = mr_malloc(sizeof(ramfs_node_t));
    if (node) {
        node->data = MR_NULL;
    } else {
        ret = -MR_ENOMEM;
        goto _exit;
    }

    /* Create inode */
    inode = mr_inode_create(type, &__ramfs_inodes, &__ramfs_files, node);
    if (inode) {
        *out = inode;
    } else {
        ret = -MR_ENOMEM;
        mr_free(node);
        goto _exit;
    }
    return 0;
_exit:
    *out = MR_NULL;
    return ret;
}

static mr_err_t ramfs_inode_stat(mr_inode_t *inode, mr_stat_t *stat) {
    MR_IGNORE(inode);
    MR_IGNORE(stat);
    return 0;
}

static mr_err_t ramfs_inode_unlink(mr_inode_t *dir, const char *name) {
    MR_IGNORE(dir);
    MR_IGNORE(name);
    return 0;
}

static mr_err_t ramfs_inode_rename(mr_inode_t *old_dir, const char *old_name,
                                   mr_inode_t *new_dir, const char *new_name) {
    MR_IGNORE(old_dir);
    MR_IGNORE(old_name);
    MR_IGNORE(new_dir);
    MR_IGNORE(new_name);
    return 0;
}

static mr_err_t ramfs_inode_truncate(mr_inode_t *inode, mr_int64_t length) {
    ramfs_node_t *node = inode->priv;
    mr_uint8_t *data;

    /* Check parameter */
    if (length < 0) {
        return -MR_EINVAL;
    }

    /* No need to change */
    if (length == inode->size) {
        return 0;
    }

    /* Truncate file */
    if (length > 0) {
        /* Check size limit */
        if ((mr_uint64_t)length > (mr_uint64_t)MR_SIZE_MAX) {
            return -MR_ENOMEM;
        }

        /* Allocate memory */
        data = mr_realloc(node->data, (mr_size_t)length);
        if (!data) {
            return -MR_ENOMEM;
        } else {
            node->data = data;
        }

        /* Init memory */
        if (length > inode->size) {
            mr_memset(node->data + inode->size, 0,
                      (mr_size_t)(length - inode->size));
        }
    } else {
        /* Free memory */
        if (node->data) {
            mr_free(node->data);
            node->data = MR_NULL;
        }
    }
    inode->size = length;
    return 0;
}

static void ramfs_inode_destroy(mr_inode_t *inode) {
    ramfs_node_t *node = inode->priv;

    /* Check parameter */
    if (!node) {
        return;
    }

    /* Destroy node */
    if (node->data) {
        mr_free(node->data);
    }
    inode->priv = MR_NULL;
    mr_free(node);
}

static mr_err_t ramfs_file_open(mr_inode_t *inode, mr_file_t *file) {
    MR_IGNORE(inode);
    MR_IGNORE(file);
    return 0;
}

static mr_err_t ramfs_file_close(mr_file_t *file) {
    MR_IGNORE(file);
    return 0;
}

static mr_ssize_t ramfs_file_read(mr_file_t *file, void *buf, mr_size_t size) {
    ramfs_node_t *node = file->inode->priv;
    mr_int64_t avail;

    /* Check parameter */
    if ((size == 0) || (file->pos >= file->inode->size)) {
        return 0;
    }

    /* Read data */
    avail = file->inode->size - file->pos;
    if ((mr_int64_t)size > avail) {
        size = (mr_size_t)avail;
    }
    mr_memcpy(buf, node->data + file->pos, size);
    file->pos += (mr_int64_t)size;
    return (mr_ssize_t)size;
}

static mr_ssize_t ramfs_file_write(mr_file_t *file, const void *buf,
                                   mr_size_t size) {
    ramfs_node_t *node = file->inode->priv;
    mr_uint8_t *data;
    mr_int64_t end;

    /* Check parameter */
    if (size == 0) {
        return 0;
    }

    /* Check overflow */
    end = file->pos + (mr_int64_t)size;
    if ((end < file->pos) || ((mr_uint64_t)end > (mr_uint64_t)MR_SIZE_MAX)) {
        return -MR_ENOMEM;
    }

    /* Allocate memory */
    if (end > file->inode->size) {
        data = mr_realloc(node->data, (mr_size_t)end);
        if (!data) {
            return -MR_ENOMEM;
        } else {
            node->data = data;
        }

        /* Zero-fill hole between old size and write position */
        if (file->pos > file->inode->size) {
            mr_memset(node->data + file->inode->size, 0,
                      (mr_size_t)(file->pos - file->inode->size));
        }
    }

    /* Write data */
    mr_memcpy(node->data + file->pos, buf, size);
    file->pos = end;
    if (end > file->inode->size) {
        file->inode->size = end;
    }
    return (mr_ssize_t)size;
}

static mr_bool_t ramfs_file_readdir(mr_file_t *file, mr_dirent_t *dirent) {
    mr_dentry_t *child;
    mr_size_t idx;

    /* Get child dentry */
    idx = (mr_size_t)file->pos;
    child = mr_dentry_child_get(file->dentry, idx);
    if (!child) {
        return MR_FALSE;
    }

    /* Read directory entry */
    mr_strncpy(dirent->name, MR_DENTRY_NAME(child), MR_CFG_FS_PATH_LEN - 1);
    dirent->name[MR_CFG_FS_PATH_LEN - 1] = '\0';
    dirent->type = child->inode ? child->inode->type : MR_INODE_NONE;
    mr_dentry_put(child);

    /* Update position */
    file->pos = (mr_int64_t)(idx + 1);
    return MR_TRUE;
}

static mr_err_t ramfs_mount(mr_super_block_t *sb, const char *source, int flags,
                            void *data) {
    ramfs_node_t *root_node;
    mr_inode_t *root_inode;
    mr_err_t ret;

    MR_IGNORE(source);
    MR_IGNORE(flags);
    MR_IGNORE(data);

    /* Create root ramfs node */
    root_node = mr_malloc(sizeof(ramfs_node_t));
    if (root_node) {
        root_node->data = MR_NULL;
    } else {
        ret = -MR_ENOMEM;
        goto _exit;
    }

    /* Create root inode */
    root_inode = mr_inode_create(MR_INODE_DIR, &__ramfs_inodes, &__ramfs_files,
                                 root_node);
    if (root_inode) {
        sb->root = root_inode;
        sb->dops = MR_NULL;
    } else {
        ret = -MR_ENOMEM;
        mr_free(root_node);
        goto _exit;
    }
    return 0;
_exit:
    return ret;
}

static void ramfs_kill_sb(mr_super_block_t *sb) {
    /* Put root inode */
    if (sb->root) {
        mr_inode_put(sb->root);
        sb->root = MR_NULL;
    }
}

static void mr_fs_ramfs_init(void) {
    mr_file_system_register(&__ramfs);
}
MR_INIT_EXPORT(mr_fs_ramfs_init, MR_INIT_LEVEL_DRIVER);
#endif /* defined(MR_USE_FS_RAMFS) */
