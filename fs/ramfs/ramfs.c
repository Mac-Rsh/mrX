/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/fs/mr_fs_file.h>
#if defined(MR_USE_FS_RAMFS)
#if defined(MR_USE_FS_RAMFS_FIFO)
#include <mrX/kernel/mr_fifo.h>
#endif /* defined(MR_USE_FS_RAMFS_FIFO) */
#include <mrX/kernel/mr_spinlock.h>
#include <mrX/libc/mr_malloc.h>
#include <mrX/libc/mr_string.h>

/* RamFS inode operations */
static mr_err_t ramfs_inode_create(mr_inode_t *dir, const char *name,
                                   mr_inode_type_t type, mr_inode_t **out);
static mr_err_t ramfs_inode_truncate(mr_inode_t *inode, mr_file_t *file,
                                     mr_int64_t length);
static void ramfs_inode_destroy(mr_inode_t *inode);
static const mr_inode_ops_t __ramfs_inodes = {
    .create = ramfs_inode_create,
    .truncate = ramfs_inode_truncate,
    .destroy = ramfs_inode_destroy,
};

/* RamFS regular file operations */
static mr_ssize_t ramfs_regular_read(mr_file_t *file, void *buf,
                                     mr_size_t size);
static mr_ssize_t ramfs_regular_write(mr_file_t *file, const void *buf,
                                      mr_size_t size);
static const mr_file_ops_t __ramfs_regular_files = {
    .read = ramfs_regular_read,
    .write = ramfs_regular_write,
};

#if defined(MR_USE_FS_RAMFS_FIFO)
/* RamFS FIFO file operations */
static mr_ssize_t ramfs_fifo_read(mr_file_t *file, void *buf, mr_size_t size);
static mr_ssize_t ramfs_fifo_write(mr_file_t *file, const void *buf,
                                   mr_size_t size);
static mr_int64_t ramfs_fifo_llseek(mr_file_t *file, mr_int64_t offset,
                                    int whence);
static const mr_file_ops_t __ramfs_fifo_files = {
    .read = ramfs_fifo_read,
    .write = ramfs_fifo_write,
    .llseek = ramfs_fifo_llseek,
};
#endif /* defined(MR_USE_FS_RAMFS_FIFO) */

/* RamFS file system operations */
static mr_err_t ramfs_mount(mr_sblock_t *sb, const char *source, int flags,
                            void *data);
static mr_file_system_t __ramfs
    = MR_FILE_SYSTEM_INIT("ramfs", ramfs_mount, MR_NULL);

/* RamFS node type */
typedef struct ramfs_node {
    mr_spinlock_t lock;
    union ramfs_node_data {
        struct ramfs_node_regular {
            mr_uint8_t *data;
        } regular;
#if defined(MR_USE_FS_RAMFS_FIFO)
        mr_fifo_t fifo;
#endif /* defined(MR_USE_FS_RAMFS_FIFO) */
    } data;
} ramfs_node_t;

static mr_err_t ramfs_inode_alloc(mr_inode_type_t type, mr_inode_t **out) {
    const mr_file_ops_t *fops;
    ramfs_node_t *node;
    mr_inode_t *inode;
    mr_err_t ret;

    /* FIFO remains a known type when the feature is off */
#if !defined(MR_USE_FS_RAMFS_FIFO)
    if (type == MR_INODE_FIFO) {
        ret = -MR_ENOSYS;
        goto _exit;
    }
#endif /* !defined(MR_USE_FS_RAMFS_FIFO) */
    if ((type != MR_INODE_DIR) && (type != MR_INODE_REG)
        && (type != MR_INODE_FIFO)) {
        ret = -MR_EINVAL;
        goto _exit;
    }

    /* Directories have no private buffer */
    if (type != MR_INODE_DIR) {
        node = mr_malloc(sizeof(ramfs_node_t));
        if (node) {
            mr_memset(node, 0, sizeof(ramfs_node_t));
            mr_spinlock_init(&node->lock);
        } else {
            ret = -MR_ENOMEM;
            goto _exit;
        }

        if (type == MR_INODE_REG) {
            fops = &__ramfs_regular_files;
        }
#if defined(MR_USE_FS_RAMFS_FIFO)
        else {
#if !defined(MR_CFG_FS_RAMFS_FIFO_SIZE)
#define MR_CFG_FS_RAMFS_FIFO_SIZE       (32U)
#endif /* !defined(MR_CFG_FS_RAMFS_FIFO_SIZE) */
            if (mr_fifo_alloc(&node->data.fifo, MR_CFG_FS_RAMFS_FIFO_SIZE, 1U)
                != 0U) {
                fops = &__ramfs_fifo_files;
            } else {
                ret = -MR_ENOMEM;
                goto _exit_free;
            }
        }
#endif /* defined(MR_USE_FS_RAMFS_FIFO) */
    } else {
        node = MR_NULL;
        fops = MR_NULL;
    }

    /* Create inode */
    inode = mr_inode_create(type, &__ramfs_inodes, fops, node);
    if (!inode) {
        ret = -MR_ENOMEM;
        goto _exit_free;
    }
    *out = inode;
    return 0;
_exit_free:
#if defined(MR_USE_FS_RAMFS_FIFO)
    if ((type == MR_INODE_FIFO) && node) {
        mr_fifo_free(&node->data.fifo);
    }
#endif /* defined(MR_USE_FS_RAMFS_FIFO) */
    if (node) {
        mr_free(node);
    }
_exit:
    *out = MR_NULL;
    return ret;
}

static mr_err_t ramfs_inode_create(mr_inode_t *dir, const char *name,
                                   mr_inode_type_t type, mr_inode_t **out) {
    MR_IGNORE(dir);
    MR_IGNORE(name);

    /* Create inode */
    return ramfs_inode_alloc(type, out);
}

static mr_err_t ramfs_node_try_lock(ramfs_node_t *node) {
    /* Try to lock node data once */
    return mr_spinlock_try_lock(&node->lock) ? 0 : -MR_EBUSY;
}

static void ramfs_node_unlock(ramfs_node_t *node) {
    /* Unlock node data */
    mr_spinlock_unlock(&node->lock);
}

static mr_err_t ramfs_inode_truncate(mr_inode_t *inode, mr_file_t *file,
                                     mr_int64_t length) {
    ramfs_node_t *node = inode->priv;
    mr_uint8_t *data;
    mr_err_t ret;

    MR_IGNORE(file);

    /* Check parameter */
    if (length < 0) {
        return -MR_EINVAL;
    }

    /* Lock node data */
    ret = ramfs_node_try_lock(node);
    if (ret != 0) {
        return ret;
    }

    /* No need to change */
    if (length == inode->size) {
        goto _exit;
    }

    /* Truncate file */
    if (length > 0) {
        /* Check size limit */
        if ((mr_uint64_t)length > (mr_uint64_t)MR_SIZE_MAX) {
            ret = -MR_ENOMEM;
            goto _exit;
        }

        /* Allocate memory */
        data = mr_realloc(node->data.regular.data, (mr_size_t)length);
        if (data) {
            node->data.regular.data = data;
        } else {
            ret = -MR_ENOMEM;
            goto _exit;
        }

        /* Init memory */
        if (length > inode->size) {
            mr_memset(node->data.regular.data + inode->size, 0,
                      (mr_size_t)(length - inode->size));
        }
    } else {
        /* Free memory */
        if (node->data.regular.data) {
            mr_free(node->data.regular.data);
            node->data.regular.data = MR_NULL;
        }
    }
    inode->size = length;
_exit:
    /* Unlock node data */
    ramfs_node_unlock(node);
    return ret;
}

static void ramfs_inode_destroy(mr_inode_t *inode) {
    ramfs_node_t *node = inode->priv;

    /* Check parameter */
    if (!node) {
        return;
    }

    /* Destroy type-specific buffers. The inode is at end of life, so no data
     * operation can hold the node lock any more */
    if (inode->type == MR_INODE_REG) {
        if (node->data.regular.data) {
            mr_free(node->data.regular.data);
        }
    }
#if defined(MR_USE_FS_RAMFS_FIFO)
    else if (inode->type == MR_INODE_FIFO) {
        mr_fifo_free(&node->data.fifo);
    }
#endif /* defined(MR_USE_FS_RAMFS_FIFO) */
    inode->priv = MR_NULL;
    mr_free(node);
}

static mr_ssize_t ramfs_regular_read(mr_file_t *file, void *buf,
                                     mr_size_t size) {
    ramfs_node_t *node = file->inode->priv;
    mr_int64_t avail;
    mr_err_t ret;

    /* Check parameter */
    if (size == 0) {
        return 0;
    }

    /* Lock node data */
    ret = ramfs_node_try_lock(node);
    if (ret != 0) {
        return (mr_ssize_t)ret;
    }

    /* Read data */
    if (file->pos < file->inode->size) {
        avail = file->inode->size - file->pos;
        if ((mr_int64_t)size > avail) {
            size = (mr_size_t)avail;
        }
        mr_memcpy(buf, node->data.regular.data + file->pos, size);
        file->pos += (mr_int64_t)size;
    } else {
        size = 0;
    }

    /* Unlock node data */
    ramfs_node_unlock(node);
    return (mr_ssize_t)size;
}

static mr_ssize_t ramfs_regular_write(mr_file_t *file, const void *buf,
                                      mr_size_t size) {
    ramfs_node_t *node = file->inode->priv;
    mr_int64_t previous_pos, end;
    mr_uint8_t *data;
    mr_ssize_t ret;

    /* Zero-length writes leave the position unchanged, including O_APPEND */
    if (size == 0) {
        return 0;
    }

    /* Lock node data */
    ret = (mr_ssize_t)ramfs_node_try_lock(node);
    if (ret != 0) {
        return ret;
    }

    /* Select EOF under the same lock as the write */
    previous_pos = file->pos;
    mr_file_apply_append(file);

    /* Check position range after append has updated the handle */
    if (((mr_uint64_t)size > (mr_uint64_t)(MR_INT64_MAX - file->pos))
        || (file->pos < 0)) {
        file->pos = previous_pos;
        ret = -MR_ERANGE;
        goto _exit;
    }
    end = file->pos + (mr_int64_t)size;
    if ((mr_uint64_t)end > (mr_uint64_t)MR_SIZE_MAX) {
        file->pos = previous_pos;
        ret = -MR_ENOMEM;
        goto _exit;
    }

    /* Allocate memory */
    if (end > file->inode->size) {
        data = mr_realloc(node->data.regular.data, (mr_size_t)end);
        if (data) {
            node->data.regular.data = data;
        } else {
            file->pos = previous_pos;
            ret = -MR_ENOMEM;
            goto _exit;
        }

        /* Zero-fill hole between old size and write position */
        if (file->pos > file->inode->size) {
            mr_memset(node->data.regular.data + file->inode->size, 0,
                      (mr_size_t)(file->pos - file->inode->size));
        }
    }

    /* Write data */
    mr_memcpy(node->data.regular.data + file->pos, buf, size);
    file->pos = end;
    if (end > file->inode->size) {
        file->inode->size = end;
    }
    ret = (mr_ssize_t)size;
_exit:
    /* Unlock node data */
    ramfs_node_unlock(node);
    return ret;
}

#if defined(MR_USE_FS_RAMFS_FIFO)
static mr_ssize_t ramfs_fifo_read(mr_file_t *file, void *buf, mr_size_t size) {
    ramfs_node_t *node = file->inode->priv;
    mr_ssize_t ret;
    mr_size_t n;

    /* Zero-length reads succeed without touching the queue */
    if (size == 0) {
        return 0;
    }

    /* Lock node data so "check length, then drain" is one transaction */
    ret = (mr_ssize_t)ramfs_node_try_lock(node);
    if (ret != 0) {
        return ret;
    }

    /* Read as many bytes as currently available */
    n = MR_MIN(size, mr_fifo_len(&node->data.fifo));
    if (n > (mr_size_t)MR_SSIZE_MAX) {
        n = (mr_size_t)MR_SSIZE_MAX;
    }
    if (n > 0) {
        ret = (mr_ssize_t)mr_fifo_read(&node->data.fifo, buf, (mr_uint32_t)n);
    }

    /* Unlock node data */
    ramfs_node_unlock(node);
    return ret;
}

static mr_ssize_t ramfs_fifo_write(mr_file_t *file, const void *buf,
                                   mr_size_t size) {
    ramfs_node_t *node = file->inode->priv;
    mr_ssize_t ret;
    mr_size_t n;

    /* Zero-length writes succeed without touching the queue */
    if (size == 0) {
        return 0;
    }

    /* Lock node data so "check space, then fill" is one transaction */
    ret = (mr_ssize_t)ramfs_node_try_lock(node);
    if (ret != 0) {
        return ret;
    }

    /* Write as many bytes as currently fit */
    n = MR_MIN(size, mr_fifo_avail(&node->data.fifo));
    if (n > (mr_size_t)MR_SSIZE_MAX) {
        n = (mr_size_t)MR_SSIZE_MAX;
    }
    if (n > 0) {
        ret = (mr_ssize_t)mr_fifo_write(&node->data.fifo, buf, (mr_uint32_t)n);
    }

    /* Unlock node data */
    ramfs_node_unlock(node);
    return ret;
}

static mr_int64_t ramfs_fifo_llseek(mr_file_t *file, mr_int64_t offset,
                                    int whence) {
    MR_IGNORE(file);
    MR_IGNORE(offset);
    MR_IGNORE(whence);
    return -MR_ESPIPE;
}
#endif /* defined(MR_USE_FS_RAMFS_FIFO) */

static mr_err_t ramfs_mount(mr_sblock_t *sb, const char *source, int flags,
                            void *data) {
    mr_inode_t *root;
    mr_err_t ret;

    MR_IGNORE(source);
    MR_IGNORE(flags);
    MR_IGNORE(data);

    /* Create root directory inode */
    ret = ramfs_inode_alloc(MR_INODE_DIR, &root);
    if (ret != 0) {
        return ret;
    }
    sb->dops = MR_NULL;
    sb->root = root;
    return 0;
}

static void mr_fs_ramfs_init(void) {
    mr_file_system_register(&__ramfs);
}
MR_INIT_EXPORT(mr_fs_ramfs_init, MR_INIT_LEVEL_FS);
#endif /* defined(MR_USE_FS_RAMFS) */
