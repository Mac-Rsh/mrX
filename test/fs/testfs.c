/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include "testfs.h"
#if defined(MR_USE_TEST) && defined(MR_USE_FS)
#include <mrX/kernel/mr_initcall.h>
#include <mrX/kernel/mr_list.h>
#include <mrX/libc/mr_malloc.h>
#include <mrX/libc/mr_string.h>
#include <mrX/fs/mr_file.h>

/* Maximum name length stored per node */
#define TESTFS_NAME_MAX                 (32)
/* Initial capacity for a regular file's data buffer */
#define TESTFS_FILE_CAPACITY            (128)

/* Testfs node: FS-private data attached to each inode via inode->priv */
typedef struct testfs_node {
    char name[TESTFS_NAME_MAX];
    mr_inode_t *inode;
    struct testfs_node *parent;
    mr_list_t link;             /* link in parent->children */
    mr_list_t children;         /* children list head (directories) */
    mr_uint8_t *data;           /* file data buffer (regular files) */
    mr_size_t capacity;         /* allocated capacity of data */
} testfs_node_t;

/* Forward declarations of the ops tables */
static const mr_inode_ops_t __testfs_iops;
static const mr_file_ops_t __testfs_fops;

/* Find a child node by name in a directory */
static testfs_node_t *testfs_find_child(testfs_node_t *dir, const char *name) {
    mr_list_t *pos;

    MR_LIST_FOR_EACH(pos, &dir->children) {
        testfs_node_t *child = MR_LIST_ENTRY(pos, testfs_node_t, link);
        if (mr_strncmp(child->name, name, TESTFS_NAME_MAX) == 0) {
            return child;
        }
    }
    return MR_NULL;
}

/* Allocate and initialise a node */
static testfs_node_t *testfs_node_alloc(const char *name, mr_inode_type_t type,
                                        testfs_node_t *parent) {
    testfs_node_t *node;

    MR_IGNORE(type);

    node = mr_malloc(sizeof(testfs_node_t));
    if (!node) {
        return MR_NULL;
    }
    mr_strncpy(node->name, name, TESTFS_NAME_MAX - 1);
    node->name[TESTFS_NAME_MAX - 1] = '\0';
    node->inode = MR_NULL;
    node->parent = parent;
    mr_list_init(&node->link);
    mr_list_init(&node->children);
    node->data = MR_NULL;
    node->capacity = 0;
    return node;
}

/* Ensure the data buffer can hold at least 'need' bytes */
static mr_err_t testfs_data_ensure(testfs_node_t *node, mr_size_t need) {
    mr_uint8_t *buf;

    if (need <= node->capacity) {
        return 0;
    }
    buf = mr_realloc(node->data, need);
    if (!buf) {
        return -MR_ENOMEM;
    }
    node->data = buf;
    node->capacity = need;
    return 0;
}

/* ---- inode operations ---- */

static mr_err_t testfs_iop_create(mr_inode_t *dir, const char *name,
                                   mr_inode_type_t type, mr_inode_t **out) {
    testfs_node_t *parent = dir->priv;
    testfs_node_t *node;
    mr_inode_t *inode;

    if (testfs_find_child(parent, name)) {
        return -MR_EEXIST;
    }
    node = testfs_node_alloc(name, type, parent);
    if (!node) {
        return -MR_ENOMEM;
    }
    inode = mr_inode_create(type, &__testfs_iops, &__testfs_fops, node);
    if (!inode) {
        mr_free(node);
        return -MR_ENOMEM;
    }
    node->inode = inode;
    mr_list_add(&parent->children, &node->link);
    *out = inode;
    return 0;
}

static mr_err_t testfs_iop_lookup(mr_inode_t *dir, const char *name,
                                   mr_inode_t **out) {
    testfs_node_t *parent = dir->priv;
    testfs_node_t *node;

    node = testfs_find_child(parent, name);
    if (!node) {
        return -MR_ENOENT;
    }
    *out = mr_inode_get(node->inode);
    return 0;
}

static mr_err_t testfs_iop_stat(mr_inode_t *inode, mr_stat_t *stat) {
    MR_IGNORE(inode);
    MR_IGNORE(stat);
    return 0;
}

static mr_err_t testfs_iop_unlink(mr_inode_t *dir, const char *name) {
    testfs_node_t *parent = dir->priv;
    testfs_node_t *node;

    node = testfs_find_child(parent, name);
    if (!node) {
        return -MR_ENOENT;
    }
    mr_list_del(&node->link);
    node->parent = MR_NULL;
    return 0;
}

static mr_err_t testfs_iop_rename(mr_inode_t *old_dir, const char *old_name,
                                   mr_inode_t *new_dir, const char *new_name) {
    testfs_node_t *old_parent = old_dir->priv;
    testfs_node_t *new_parent = new_dir->priv;
    testfs_node_t *node;

    node = testfs_find_child(old_parent, old_name);
    if (!node) {
        return -MR_ENOENT;
    }
    if (testfs_find_child(new_parent, new_name)) {
        return -MR_EEXIST;
    }
    mr_list_del(&node->link);
    mr_strncpy(node->name, new_name, TESTFS_NAME_MAX - 1);
    node->name[TESTFS_NAME_MAX - 1] = '\0';
    node->parent = new_parent;
    mr_list_add(&new_parent->children, &node->link);
    return 0;
}

static mr_err_t testfs_iop_truncate(mr_inode_t *inode, mr_int64_t length) {
    testfs_node_t *node = inode->priv;
    mr_err_t ret;

    if (length < 0) {
        return -MR_EINVAL;
    }
    ret = testfs_data_ensure(node, (mr_size_t)length);
    if (ret != 0) {
        return ret;
    }
    if (length < inode->size) {
        mr_memset(node->data + length, 0,
                  (mr_size_t)(inode->size - length));
    }
    inode->size = length;
    return 0;
}

static void testfs_iop_destroy(mr_inode_t *inode) {
    testfs_node_t *node = inode->priv;

    if (!node) {
        return;
    }
    mr_list_del(&node->link);
    if (node->data) {
        mr_free(node->data);
    }
    mr_free(node);
    inode->priv = MR_NULL;
}

static const mr_inode_ops_t __testfs_iops = {
    .create = testfs_iop_create,
    .lookup = testfs_iop_lookup,
    .stat = testfs_iop_stat,
    .unlink = testfs_iop_unlink,
    .rename = testfs_iop_rename,
    .truncate = testfs_iop_truncate,
    .destroy = testfs_iop_destroy,
};

/* ---- file operations ---- */

static mr_err_t testfs_fop_open(mr_inode_t *inode, mr_file_t *file) {
    MR_IGNORE(inode);
    file->priv = MR_NULL;
    return 0;
}

static mr_err_t testfs_fop_close(mr_file_t *file) {
    MR_IGNORE(file);
    return 0;
}

static mr_ssize_t testfs_fop_read(mr_file_t *file, void *buf, mr_size_t size) {
    testfs_node_t *node = file->inode->priv;
    mr_int64_t avail;

    if (file->pos >= file->inode->size) {
        return 0;
    }
    avail = file->inode->size - file->pos;
    if ((mr_int64_t)size > avail) {
        size = (mr_size_t)avail;
    }
    mr_memcpy(buf, node->data + file->pos, size);
    file->pos += (mr_int64_t)size;
    return (mr_ssize_t)size;
}

static mr_ssize_t testfs_fop_write(mr_file_t *file, const void *buf,
                                    mr_size_t size) {
    testfs_node_t *node = file->inode->priv;
    mr_err_t ret;

    ret = testfs_data_ensure(node, (mr_size_t)(file->pos + (mr_int64_t)size));
    if (ret != 0) {
        return ret;
    }
    mr_memcpy(node->data + file->pos, buf, size);
    file->pos += (mr_int64_t)size;
    if (file->pos > file->inode->size) {
        file->inode->size = file->pos;
    }
    return (mr_ssize_t)size;
}

static mr_bool_t testfs_fop_readdir(mr_file_t *file, mr_dirent_t *dirent) {
    testfs_node_t *dir = file->inode->priv;
    mr_list_t *pos;
    mr_size_t i, idx;

    idx = (mr_size_t)file->pos;
    i = 0;
    MR_LIST_FOR_EACH(pos, &dir->children) {
        if (i == idx) {
            testfs_node_t *child = MR_LIST_ENTRY(pos, testfs_node_t, link);
            mr_strncpy(dirent->name, child->name, MR_CFG_FS_FILE_NAME_LEN - 1);
            dirent->name[MR_CFG_FS_FILE_NAME_LEN - 1] = '\0';
            dirent->type = child->inode->type;
            file->pos = (mr_int64_t)(idx + 1);
            return MR_TRUE;
        }
        i++;
    }
    return MR_FALSE;
}

static const mr_file_ops_t __testfs_fops = {
    .open = testfs_fop_open,
    .close = testfs_fop_close,
    .read = testfs_fop_read,
    .write = testfs_fop_write,
    .readdir = testfs_fop_readdir,
};

/* ---- file-system type ---- */

mr_err_t testfs_mount(mr_super_block_t *sb, const char *source, int flags,
                      void *data) {
    testfs_node_t *root_node;
    mr_inode_t *root_inode;

    MR_IGNORE(source);
    MR_IGNORE(flags);
    MR_IGNORE(data);

    root_node = testfs_node_alloc("", MR_INODE_DIR, MR_NULL);
    if (!root_node) {
        return -MR_ENOMEM;
    }
    root_inode = mr_inode_create(MR_INODE_DIR, &__testfs_iops, &__testfs_fops,
                                 root_node);
    if (!root_inode) {
        mr_free(root_node);
        return -MR_ENOMEM;
    }
    root_node->inode = root_inode;
    sb->root = root_inode;
    sb->dops = MR_NULL;
    return 0;
}

void testfs_kill_sb(mr_super_block_t *sb) {
    if (sb->root) {
        mr_inode_put(sb->root);
        sb->root = MR_NULL;
    }
}

/* Auto-registered file-system objects */
static mr_file_system_t __testfs = MR_FILE_SYSTEM_INIT(TESTFS_NAME,
                                                       testfs_mount,
                                                       testfs_kill_sb);

/* A file-system whose mount always fails (for failure-path tests) */
static mr_err_t testfs_fail_mount(mr_super_block_t *sb, const char *source,
                                   int flags, void *data) {
    MR_IGNORE(sb);
    MR_IGNORE(source);
    MR_IGNORE(flags);
    MR_IGNORE(data);
    return -MR_EIO;
}

static void testfs_fail_kill_sb(mr_super_block_t *sb) {
    MR_IGNORE(sb);
}

static mr_file_system_t __testfs_fail = MR_FILE_SYSTEM_INIT(TESTFS_FAIL_NAME,
                                                            testfs_fail_mount,
                                                            testfs_fail_kill_sb);

mr_file_system_t *testfs_get(void) {
    return mr_file_system_find(TESTFS_NAME);
}

static void testfs_init(void) {
    /* The static objects are already initialised via MR_FILE_SYSTEM_INIT, so
     * only register them here */
    (void)mr_file_system_register(&__testfs);
    (void)mr_file_system_register(&__testfs_fail);
}
MR_INIT_EXPORT(testfs_init, MR_INIT_LEVEL_DRIVER);
#endif /* defined(MR_USE_TEST) && defined(MR_USE_FS) */
