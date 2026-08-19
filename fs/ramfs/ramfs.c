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

/* FS name */
#define FS_NAME                         "ramfs"

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
static mr_ssize_t ramfs_fifo_fcntl(mr_file_t *file, int cmd, mr_intptr_t arg);
static const mr_file_ops_t __ramfs_fifo_files = {
    .read = ramfs_fifo_read,
    .write = ramfs_fifo_write,
    .llseek = ramfs_fifo_llseek,
    .fcntl = ramfs_fifo_fcntl,
};
#endif /* defined(MR_USE_FS_RAMFS_FIFO) */

/* RamFS file system operations */
static mr_err_t ramfs_mount(mr_super_block_t *sb, const char *source, int flags,
                            void *data);
static mr_file_system_t __ramfs
    = MR_FILE_SYSTEM_INIT(FS_NAME, ramfs_mount, MR_NULL, MR_NULL);

/* RamFS node type */
typedef union ramfs_node {
    struct ramfs_node_regular {
        mr_uint8_t *data;
    } regular;
#if defined(MR_USE_FS_RAMFS_FIFO)
    struct ramfs_node_fifo {
        mr_size_t capacity;
        mr_uint8_t *data;
        mr_size_t head;
        mr_size_t len;
    } fifo;
#endif /* defined(MR_USE_FS_RAMFS_FIFO) */
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
            node->fifo.capacity = MR_CFG_FS_RAMFS_FIFO_SIZE;
            node->fifo.data = mr_malloc(node->fifo.capacity);
            if (node->fifo.data) {
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
    if ((type == MR_INODE_FIFO) && node && node->fifo.data) {
        mr_free(node->fifo.data);
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

static mr_err_t ramfs_inode_truncate(mr_inode_t *inode, mr_file_t *file,
                                     mr_int64_t length) {
    ramfs_node_t *node = file->inode->priv;
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
        data = mr_realloc(node->regular.data, (mr_size_t)length);
        if (data) {
            node->regular.data = data;
        } else {
            return -MR_ENOMEM;
        }

        /* Init memory */
        if (length > inode->size) {
            mr_memset(node->regular.data + inode->size, 0,
                      (mr_size_t)(length - inode->size));
        }
    } else {
        /* Free memory */
        if (node->regular.data) {
            mr_free(node->regular.data);
            node->regular.data = MR_NULL;
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

    /* Destroy type-specific buffers */
    if (inode->type == MR_INODE_REG) {
        if (node->regular.data) {
            mr_free(node->regular.data);
        }
    }
#if defined(MR_USE_FS_RAMFS_FIFO)
    else if (inode->type == MR_INODE_FIFO) {
        if (node->fifo.data) {
            mr_free(node->fifo.data);
        }
    }
#endif /* defined(MR_USE_FS_RAMFS_FIFO) */
    inode->priv = MR_NULL;
    mr_free(node);
}

static mr_ssize_t ramfs_regular_read(mr_file_t *file, void *buf,
                                     mr_size_t size) {
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
    mr_memcpy(buf, node->regular.data + file->pos, size);
    file->pos += (mr_int64_t)size;
    return (mr_ssize_t)size;
}

static mr_ssize_t ramfs_regular_write(mr_file_t *file, const void *buf,
                                      mr_size_t size) {
    ramfs_node_t *node = file->inode->priv;
    mr_uint8_t *data;
    mr_int64_t end;

    /* Check parameter */
    if (size == 0) {
        return 0;
    }

    /* Check position range before conversion and addition */
    if (((mr_uint64_t)size > (mr_uint64_t)(MR_INT64_MAX - file->pos))
        || (file->pos < 0)) {
        return -MR_ERANGE;
    }
    end = file->pos + (mr_int64_t)size;
    if ((mr_uint64_t)end > (mr_uint64_t)MR_SIZE_MAX) {
        return -MR_ENOMEM;
    }

    /* Allocate memory */
    if (end > file->inode->size) {
        data = mr_realloc(node->regular.data, (mr_size_t)end);
        if (data) {
            node->regular.data = data;
        } else {
            return -MR_ENOMEM;
        }

        /* Zero-fill hole between old size and write position */
        if (file->pos > file->inode->size) {
            mr_memset(node->regular.data + file->inode->size, 0,
                      (mr_size_t)(file->pos - file->inode->size));
        }
    }

    /* Write data */
    mr_memcpy(node->regular.data + file->pos, buf, size);
    file->pos = end;
    if (end > file->inode->size) {
        file->inode->size = end;
    }
    return (mr_ssize_t)size;
}

#if defined(MR_USE_FS_RAMFS_FIFO)
static void ramfs_fifo_copy_out(const ramfs_node_t *node, mr_size_t offset,
                                void *buf, mr_size_t size) {
    const mr_uint8_t *data = node->fifo.data;
    mr_size_t capacity = node->fifo.capacity;
    mr_size_t pos = node->fifo.head + offset;
    mr_size_t first;

    /* Convert logical offset to ring index */
    if (pos >= capacity) {
        pos -= capacity;
    }

    /* Copy the tail, then the wrapped head */
    first = MR_MIN(size, capacity - pos);
    mr_memcpy(buf, data + pos, first);
    if (first < size) {
        mr_memcpy((mr_uint8_t *)buf + first, data, size - first);
    }
}

static void ramfs_fifo_copy_in(ramfs_node_t *node, const void *buf,
                               mr_size_t size) {
    mr_size_t pos = node->fifo.head + node->fifo.len;
    mr_size_t capacity = node->fifo.capacity;
    mr_uint8_t *data = node->fifo.data;
    mr_size_t first;

    /* Convert write offset to ring index */
    if (pos >= capacity) {
        pos -= capacity;
    }

    /* Copy the tail, then the wrapped head */
    first = MR_MIN(size, capacity - pos);
    mr_memcpy(data + pos, buf, first);
    if (first < size) {
        mr_memcpy(data, (const mr_uint8_t *)buf + first, size - first);
    }
}

static mr_ssize_t ramfs_fifo_read(mr_file_t *file, void *buf, mr_size_t size) {
    ramfs_node_t *node = file->inode->priv;
    mr_size_t n;

    /* Zero-length reads succeed without touching the queue */
    if (size == 0) {
        return 0;
    }
    if (node->fifo.len == 0) {
        return -MR_EAGAIN;
    }

    /* Read as many bytes as currently available */
    n = MR_MIN(size, node->fifo.len);
    if (n > (mr_size_t)MR_SSIZE_MAX) {
        n = (mr_size_t)MR_SSIZE_MAX;
    }
    ramfs_fifo_copy_out(node, 0, buf, n);
    node->fifo.head += n;
    if (node->fifo.head >= node->fifo.capacity) {
        node->fifo.head -= node->fifo.capacity;
    }
    node->fifo.len -= n;
    return (mr_ssize_t)n;
}

static mr_ssize_t ramfs_fifo_write(mr_file_t *file, const void *buf,
                                   mr_size_t size) {
    ramfs_node_t *node = file->inode->priv;
    mr_size_t space, n;

    /* Zero-length writes succeed without touching the queue */
    if (size == 0) {
        return 0;
    }
    space = node->fifo.capacity - node->fifo.len;
    if (space == 0) {
        return -MR_EAGAIN;
    }

    /* Write as many bytes as currently fit */
    n = MR_MIN(size, space);
    if (n > (mr_size_t)MR_SSIZE_MAX) {
        n = (mr_size_t)MR_SSIZE_MAX;
    }
    ramfs_fifo_copy_in(node, buf, n);
    node->fifo.len += n;
    return (mr_ssize_t)n;
}

static mr_int64_t ramfs_fifo_llseek(mr_file_t *file, mr_int64_t offset,
                                    int whence) {
    MR_IGNORE(file);
    MR_IGNORE(offset);
    MR_IGNORE(whence);
    return -MR_ESPIPE;
}

static mr_ssize_t ramfs_fifo_fcntl(mr_file_t *file, int cmd, mr_intptr_t arg) {
    ramfs_node_t *node = file->inode->priv;
    mr_uint8_t *new_data, *old_data;
    mr_size_t requested;
    mr_size_t n;

    switch (cmd) {
        case MR_F_GETPIPE_SZ: {
            if (arg != 0) {
                return -MR_EINVAL;
            }
            return (mr_ssize_t)node->fifo.capacity;
        }
        case MR_F_SETPIPE_SZ: {
            if (arg <= 0) {
                return -MR_EINVAL;
            }
            if ((mr_uintptr_t)arg > (mr_uintptr_t)MR_SSIZE_MAX) {
                return -MR_ERANGE;
            }
            requested = (mr_size_t)arg;
            if (requested == node->fifo.capacity) {
                return (mr_ssize_t)requested;
            }
            if (node->fifo.len > requested) {
                return -MR_EBUSY;
            }

            /* Allocate the new buffer before touching the current one */
            new_data = mr_malloc(requested);
            if (!new_data) {
                return -MR_ENOMEM;
            }

            /* Preserve unread bytes in logical order at the new origin */
            n = node->fifo.len;
            if (n > 0) {
                ramfs_fifo_copy_out(node, 0, new_data, n);
            }
            old_data = node->fifo.data;
            node->fifo.capacity = requested;
            node->fifo.data = new_data;
            node->fifo.head = 0;
            mr_free(old_data);
            return (mr_ssize_t)requested;
        }
        default: {
            return -MR_EINVAL;
        }
    }
}
#endif /* defined(MR_USE_FS_RAMFS_FIFO) */

static mr_err_t ramfs_mount(mr_super_block_t *sb, const char *source, int flags,
                            void *data) {
    mr_inode_t *root_inode;
    mr_err_t ret;

    MR_IGNORE(source);
    MR_IGNORE(flags);
    MR_IGNORE(data);

    /* Create root directory inode */
    ret = ramfs_inode_alloc(MR_INODE_DIR, &root_inode);
    if (ret != 0) {
        return ret;
    }
    sb->root = root_inode;
    sb->dops = MR_NULL;
    return 0;
}

static void mr_fs_ramfs_init(void) {
    mr_file_system_register(&__ramfs);
}
MR_INIT_EXPORT(mr_fs_ramfs_init, MR_INIT_LEVEL_FS);
#endif /* defined(MR_USE_FS_RAMFS) */
