/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/fs/mr_fs_file.h>
#if defined(MR_USE_FS)
#include <mrX/fs/mr_fs_struct.h>
#include <mrX/libc/mr_malloc.h>
#include <mrX/libc/mr_string.h>

/* FS node operation definition */
typedef enum fs_node_op {
    FS_NODE_OP_CREATE,
    FS_NODE_OP_MKNOD,
} fs_node_op_t;

/* FS path split type */
typedef struct fs_path_split {
    char ipath[MR_CFG_FS_INLINE_NAME_LEN * 4];
    const char *leaf;
    char *parent;
} fs_path_split_t;

static mr_err_t fs_struct_snapshot(mr_fs_struct_t *fs_struct, const char *path,
                                   mr_dentry_t **start, mr_dentry_t **root) {
    int mask;

    /* Lock */
    mask = mr_fs_struct_lock();

    /* Pin root and lookup start while the structure is stable */
    *start = mr_dentry_get((path[0] == '/') ? fs_struct->root : fs_struct->cwd);
    *root = mr_dentry_get(fs_struct->root);

    /* Unlock */
    mr_fs_struct_unlock(mask);

    /* Check pin result */
    if ((!(*root)) || (!(*start))) {
        if (*root) {
            mr_dentry_put(*root);
        }
        if (*start) {
            mr_dentry_put(*start);
        }
        return -MR_ENOENT;
    }
    return 0;
}

static mr_err_t fs_op_enter_by_dentry(mr_dentry_t *dentry, mr_sblock_t **out) {
    mr_inode_t *inode;
    mr_sblock_t *sb;
    mr_err_t ret;
    int mask;

    /* Lock */
    mask = mr_fs_struct_lock();

    /* Pin the inode while its dentry binding is stable */
    inode = (dentry && dentry->inode) ? mr_inode_get(dentry->inode) : MR_NULL;

    /* Unlock */
    mr_fs_struct_unlock(mask);

    /* Check inode */
    if (!inode) {
        return -MR_ENOENT;
    }

    /* Pin and enter the inode's super-block operation */
    sb = mr_sblock_get(inode->sb);
    mr_inode_put(inode);
    if (!sb) {
        return -MR_ENOENT;
    }
    ret = mr_sblock_op_enter(sb);
    if (ret != 0) {
        mr_sblock_put(sb);
        return ret;
    }
    *out = sb;
    return 0;
}

static void fs_op_exit(mr_sblock_t *sb) {
    /* Leave and release the super-block */
    mr_sblock_op_exit(sb);
    mr_sblock_put(sb);
}

static mr_err_t fs_op_enter_by_file(const mr_file_t *file, mr_sblock_t **out) {
    mr_sblock_t *sb;
    mr_err_t ret;

    /* Pin the super-block of an already-open handle */
    sb = file->inode ? mr_sblock_get(file->inode->sb) : MR_NULL;
    if (!sb) {
        return -MR_ENOENT;
    }
    ret = mr_sblock_op_enter(sb);
    if (ret != 0) {
        mr_sblock_put(sb);
        return ret;
    }
    *out = sb;
    return 0;
}

static mr_err_t fs_lookup(mr_fs_struct_t *fs_struct, const char *path,
                          mr_dentry_t **result, mr_dentry_t **parent,
                          char **name) {
    mr_size_t offset, begin, rest, len, leaf_len;
    mr_dentry_t *current, *next, *root;
    char *pending;
    mr_err_t ret;
    char saved;

    /* Init result */
    *result = MR_NULL;
    if (parent) {
        *parent = MR_NULL;
    }
    if (name) {
        *name = MR_NULL;
    }

    /* Check path length */
    len = mr_strlen(path);
    if (len >= MR_CFG_FS_PATH_LEN) {
        return -MR_ENAMETOOLONG;
    }

    /* Copy the path because components are temporarily terminated */
    pending = mr_malloc(len + 1U);
    if (!pending) {
        return -MR_ENOMEM;
    }
    mr_strncpy(pending, path, len);
    pending[len] = '\0';

    /* Snapshot lookup context */
    ret = fs_struct_snapshot(fs_struct, path, &current, &root);
    if (ret != 0) {
        mr_free(pending);
        return ret;
    }

    /* Resolve every component through directories, '.', '..' and mounts */
    offset = 0;
    while (1) {
        while (pending[offset] == '/') {
            offset++;
        }
        if (pending[offset] == '\0') {
            /* Follow a mount on an otherwise empty path */
            ret = mr_dentry_lookup(current, root, ".", &next);
            mr_dentry_put(current);
            if (ret != 0) {
                goto _exit;
            }
            current = next;
            break;
        }

        /* Isolate one component */
        begin = offset;
        while ((pending[offset] != '\0') && (pending[offset] != '/')) {
            offset++;
        }
        rest = offset;
        while (pending[rest] == '/') {
            rest++;
        }
        saved = pending[offset];
        pending[offset] = '\0';

        /* Resolve one component through the dentry cache or file-system */
        ret = mr_dentry_lookup(current, root, &pending[begin], &next);
        pending[offset] = saved;
        if (ret != 0) {
            /* Return a valid missing final component to the creator */
            if ((ret == (-MR_ENOENT)) && parent && name
                && (pending[rest] == '\0')) {
                if (saved == '/') {
                    ret = -MR_EINVAL;
                } else {
                    leaf_len = offset - begin;
                    mr_memmove(pending, &pending[begin], leaf_len);
                    pending[leaf_len] = '\0';
                    *name = pending;
                    pending = MR_NULL;
                    *parent = current;
                    current = MR_NULL;
                }
            }
            goto _exit_current;
        }

        /* Continue from the resolved dentry */
        mr_dentry_put(current);
        current = next;
        if (saved == '\0') {
            break;
        }
        offset = rest;
    }

    /* Return resolved dentry */
    mr_dentry_put(root);
    mr_free(pending);
    *result = current;
    return 0;
_exit_current:
    if (current) {
        mr_dentry_put(current);
    }
_exit:
    mr_dentry_put(root);
    mr_free(pending);
    return ret;
}

static mr_err_t fs_mount_dentry(const char *source, mr_dentry_t *mnt,
                                mr_file_system_t *fs, int flags, void *data) {
    mr_sblock_t *sb, *guard, *existing;
    mr_bool_t root_mount, reserved;
    mr_dentry_t *root;
    mr_err_t ret;
    int mask;

    /* Protect the covered super-block while attaching a child mount. The
     * initial global root has no super-block to protect */
    root_mount = mr_dentry_is_root(mnt);
    reserved = MR_FALSE;
    guard = MR_NULL;
    ret = fs_op_enter_by_dentry(mnt, &guard);
    if ((ret != 0) && ((!root_mount) || (ret != (-MR_ENOENT)))) {
        goto _exit;
    }

    /* Reject a repeated mount and validate the covered node */
    existing = mr_sblock_find_by_dentry(mnt);
    if (existing) {
        mr_sblock_put(existing);
        ret = -MR_EBUSY;
        goto _exit;
    }
    if (root_mount) {
        if (mnt->inode) {
            ret = -MR_EBUSY;
            goto _exit;
        }
    } else if ((!mnt->inode) || (mnt->inode->type != MR_INODE_DIR)) {
        ret = mnt->inode ? -MR_ENOTDIR : -MR_ENOENT;
        goto _exit;
    }

    /* Keep the covered name and parent stable across the mount callback */
    if (!root_mount) {
        mask = mr_fs_struct_lock();
        if ((mnt->state == MR_DENTRY_STATE_DEAD)
            || (!MR_DENTRY_IS_ADDED(mnt))) {
            ret = -MR_ENOENT;
        } else if ((mnt->state != MR_DENTRY_STATE_ACTIVE) || mnt->mounted) {
            ret = -MR_EBUSY;
        } else {
            mnt->state = MR_DENTRY_STATE_MOUNTING;
            reserved = MR_TRUE;
            ret = 0;
        }
        mr_fs_struct_unlock(mask);
        if (ret != 0) {
            goto _exit;
        }
    }

    /* Create and mount the super-block */
    ret = mr_sblock_create(fs, source, flags, data, &sb);
    if (ret != 0) {
        goto _exit;
    }

    /* Create a separate mounted root for nested mounts. Global root is
     * the only dentry mounted in place */
    if (!root_mount) {
        root = mr_dentry_create(sb->dops, "%s", MR_DENTRY_NAME(mnt));
        if (!root) {
            mr_sblock_del(sb);
            ret = -MR_ENOMEM;
            goto _exit;
        }
    } else {
        root = mnt;
    }

    /* Bind and attach the mounted root */
    ret = mr_dentry_instantiate(root, sb->root);
    if (ret != 0) {
        if (!root_mount) {
            mr_dentry_del(root);
        }
        mr_sblock_del(sb);
        goto _exit;
    }
    ret = mr_sblock_attach(sb, mnt, root);
    if (ret != 0) {
        if (root_mount) {
            mr_dentry_uninstantiate(root);
        } else {
            mr_dentry_del(root);
        }
        mr_sblock_del(sb);
        goto _exit;
    }

    /* Super-block owns the mounted root reference */
    if (!root_mount) {
        mr_dentry_put(root);
    }
_exit:
    /* Release the covered dentry reservation on both success and failure */
    if (reserved) {
        mask = mr_fs_struct_lock();
        mnt->state = MR_DENTRY_STATE_ACTIVE;
        mr_fs_struct_unlock(mask);
    }

    /* Put dentry */
    if (guard) {
        fs_op_exit(guard);
    }
    mr_dentry_put(mnt);
    return ret;
}

mr_err_t mr_fs_mount_root(const char *source, const char *fstype, int flags,
                          void *data) {
    mr_file_system_t *fs;
    mr_dentry_t *root;

    /* Initialization-only: does not retarget live structure root/cwd */
    /* Check parameter */
    MR_ASSERT((fstype != MR_NULL) && (fstype[0] != '\0'));

    /* Find FS */
    fs = mr_file_system_find(fstype);
    if (!fs) {
        return -MR_ENOENT;
    }

    /* Get global FS root */
    root = mr_dentry_get(mr_fs_struct_root());
    if (!root) {
        return -MR_EINVAL;
    }
    return fs_mount_dentry(source, root, fs, flags, data);
}

mr_err_t mr_fs_mount(const char *source, const char *path, const char *fstype,
                     int flags, void *data) {
    mr_fs_struct_t *fs_struct;
    mr_file_system_t *fs;
    mr_dentry_t *mnt;
    mr_err_t ret;

    /* Check parameter */
    MR_ASSERT((fstype != MR_NULL) && (fstype[0] != '\0'));
    MR_ASSERT((path != MR_NULL) && (path[0] != '\0'));

    /* Get current file-system structure */
    fs_struct = mr_fs_struct_current();
    if (!fs_struct) {
        return -MR_EINVAL;
    }

    /* Find FS */
    fs = mr_file_system_find(fstype);
    if (!fs) {
        return -MR_ENOENT;
    }

    /* Resolve the mount-point. Mount never creates structure paths; rootfs or
     * the mounted parent file-system provides the directory structure */
    ret = fs_lookup(fs_struct, path, &mnt, MR_NULL, MR_NULL);
    if (ret != 0) {
        return ret;
    }
    return fs_mount_dentry(source, mnt, fs, flags, data);
}

static void fs_purge_children(mr_dentry_t *dentry) {
    mr_dentry_t *current, *child, *parent;

    /* Walk to each leaf and delete the subtree in post-order */
    current = mr_dentry_get(dentry);
    while (current) {
        child = mr_dentry_child_get(current, 0);
        if (child) {
            mr_dentry_put(current);
            current = child;
            continue;
        }

        /* Root itself is retained */
        if (current == dentry) {
            mr_dentry_put(current);
            break;
        }

        /* Keep the parent while deleting this leaf */
        parent = mr_dentry_get(current->upper);
        if (mr_dentry_del(current) != 0) {
            /* The leaf is in a transient state(moving, unlinking) and stays
             * in its parent's child list. Stop the walk instead of picking
             * the same child again forever; the remaining objects live until
             * their references are released */
            mr_dentry_put(current);
            if (parent) {
                mr_dentry_put(parent);
            }
            break;
        }
        mr_dentry_put(current);
        current = parent;
    }
}

static mr_err_t fs_sb_reclaim(mr_sblock_t *sb) {
    mr_dentry_t *mnt, *root;
    mr_err_t ret;

    /* Hide the mount and drop topology links. Remaining objects live until
     * the last reference is released */
    ret = mr_sblock_detach(sb, &mnt, &root);
    if (ret != 0) {
        return ret;
    }

    /* Tear down the mounted subtree */
    fs_purge_children(root);

    /* Delete the super-block. This drops the root inode's creation reference;
     * final backend cleanup waits for every remaining reference */
    ret = mr_sblock_del(sb);

    /* Delete the separate mounted root or clear the global root inode */
    if (mr_dentry_is_root(root)) {
        mr_dentry_uninstantiate(root);
        mr_dentry_put(root);
    } else {
        mr_dentry_del(root);
    }
    mr_dentry_put(mnt);
    return ret;
}

mr_err_t mr_fs_umount(const char *path) {
    mr_fs_struct_t *fs_struct;
    mr_dentry_t *dentry;
    mr_sblock_t *sb;
    mr_err_t ret;

    /* Check parameter */
    MR_ASSERT((path != MR_NULL) && (path[0] != '\0'));

    /* Get current file-system structure */
    fs_struct = mr_fs_struct_current();
    if (!fs_struct) {
        return -MR_EINVAL;
    }

    /* Resolve the visible mounted root */
    ret = fs_lookup(fs_struct, path, &dentry, MR_NULL, MR_NULL);
    if (ret != 0) {
        return ret;
    }

    /* Find the super-block mounted at this path */
    sb = mr_sblock_find_by_dentry(dentry);
    if (!sb) {
        mr_dentry_put(dentry);
        return -MR_ENOENT;
    }

    /* Atomically check idle state and detach from the structure. Backend
     * resources are released after the final object reference */
    ret = fs_sb_reclaim(sb);
    if (ret == (-MR_ENOENT)) {
        ret = 0;
    }

    /* Put super-block and dentry */
    mr_sblock_put(sb);
    mr_dentry_put(dentry);
    return ret;
}

mr_err_t mr_fs_media_detach(const char *path) {
    mr_fs_struct_t *fs_struct;
    mr_sblock_t *sb, *leaf;
    mr_dentry_t *dentry;
    mr_err_t ret;

    /* Check parameter */
    MR_ASSERT((path != MR_NULL) && (path[0] != '\0'));

    /* Get current file-system structure */
    fs_struct = mr_fs_struct_current();
    if (!fs_struct) {
        return -MR_EINVAL;
    }

    /* Resolve the visible mounted root */
    ret = fs_lookup(fs_struct, path, &dentry, MR_NULL, MR_NULL);
    if (ret != 0) {
        return ret;
    }

    /* Find the super-block mounted at this path */
    sb = mr_sblock_find_by_dentry(dentry);
    if (!sb) {
        mr_dentry_put(dentry);
        return -MR_ENOENT;
    }

    /* Mark the whole subtree OFFLINE and hide it from new lookups. This
     * never returns to ACTIVE, even if later reclaim is delayed */
    mr_sblock_offline_subtree(sb);

    /* Reclaim idle leaves without invoking the backend or issuing device I/O.
     * A busy operation leaves the whole subtree OFFLINE for a later retry of
     * the same path. Covering may already be cleared; find_by_dentry still
     * matches sb->mnt */
    while (1) {
        leaf = mr_sblock_subtree_leaf_get(sb);
        if (!leaf) {
            ret = -MR_ENOENT;
            break;
        }

        /* Namespace detach may already have completed on a racing caller */
        ret = fs_sb_reclaim(leaf);
        if (ret == (-MR_ENOENT)) {
            ret = 0;
        }
        if (ret != 0) {
            mr_sblock_put(leaf);
            break;
        }
        if (leaf == sb) {
            mr_sblock_put(leaf);
            break;
        }
        mr_sblock_put(leaf);
    }

    /* Put super-block and dentry */
    mr_sblock_put(sb);
    mr_dentry_put(dentry);
    return ret;
}

static mr_err_t fs_split_path(const char *path, fs_path_split_t *split) {
    mr_size_t len, parent_len;
    mr_bool_t found;

    /* Init split */
    split->parent = split->ipath;
    split->leaf = MR_NULL;

    /* Get path length */
    for (found = MR_FALSE, parent_len = 0, len = 0; path[len] != '\0'; len++) {
        /* Find the last '/' */
        if (path[len] == '/') {
            found = MR_TRUE;
            parent_len = len;
        }
    }

    /* Check full path length */
    if (len >= MR_CFG_FS_PATH_LEN) {
        return -MR_ENAMETOOLONG;
    }
    if (len == 0) {
        return -MR_EINVAL;
    }

    /* Split into parent + leaf */
    if (found) {
        /* Reject trailing '/' */
        if (path[parent_len + 1] == '\0') {
            return -MR_EINVAL;
        }

        /* Allocate parent name */
        if (parent_len > 0) {
            if (parent_len >= sizeof(split->ipath)) {
                /* Use dynamic name */
                split->parent = mr_malloc(parent_len + 1);
                if (!split->parent) {
                    return -MR_ENOMEM;
                }
            }
            mr_strncpy(split->parent, path, parent_len);
            split->parent[parent_len] = '\0';
        } else {
            split->ipath[0] = '/';
            split->ipath[1] = '\0';
        }
        split->leaf = &path[parent_len + 1];
    } else {
        /* No '/': parent is CWD */
        split->ipath[0] = '.';
        split->ipath[1] = '\0';
        split->leaf = path;
    }

    /* Reject empty, '.', '..' leaf */
    if ((split->leaf[0] == '\0') || (mr_strcmp(split->leaf, ".") == 0)
        || (mr_strcmp(split->leaf, "..") == 0)) {
        /* Free dynamic name */
        if (split->parent != split->ipath) {
            mr_free(split->parent);
        }
        return -MR_EINVAL;
    }
    return 0;
}

static void fs_split_path_free(fs_path_split_t *split) {
    /* Free dynamic parent name */
    if (split->parent != split->ipath) {
        mr_free(split->parent);
    }
}

static mr_err_t fs_create_node_op(mr_dentry_t *parent, const char *name,
                                  mr_inode_type_t type, fs_node_op_t op,
                                  void *priv, mr_dentry_t **dentry) {
    mr_dentry_t *child, *existing;
    mr_inode_t *inode;
    mr_sblock_t *sb;
    mr_err_t ret;

    /* Init inode */
    inode = MR_NULL;

    /* Check whether the parent is a directory */
    if ((!parent->inode) || (parent->inode->type != MR_INODE_DIR)) {
        return -MR_ENOTDIR;
    }

    /* Check the FS supports the requested operation */
    switch (op) {
        case FS_NODE_OP_CREATE: {
            if ((!parent->inode->ops) || (!parent->inode->ops->create)) {
                return -MR_ENOSYS;
            }
            break;
        }
        case FS_NODE_OP_MKNOD: {
            if ((!parent->inode->ops)
                || ((!parent->inode->ops->mknod)
                    && (!parent->inode->ops->create))) {
                return -MR_ENOSYS;
            }
            break;
        }
    }

    /* Check whether the name is existing */
    ret = mr_dentry_lookup(parent, parent, name, &existing);
    if (ret == 0) {
        /* Repeated create */
        mr_dentry_put(existing);
        return -MR_EEXIST;
    } else if (ret != (-MR_ENOENT)) {
        return ret;
    }

    /* Reserve the dentry and super-block */
    sb = parent->inode->sb;
    child = mr_dentry_create(sb ? sb->dops : MR_NULL, "%s", name);
    if (!child) {
        return -MR_ENOMEM;
    }
    if (sb) {
        sb = mr_sblock_get(sb);
        if (!sb) {
            ret = -MR_ENOENT;
            goto _exit;
        }
    }

    /* Reserve the destination in the dentry tree */
    ret = mr_dentry_add(child, parent);
    if (ret != 0) {
        goto _exit;
    }

    /* Create backing inode */
    switch (op) {
        case FS_NODE_OP_CREATE: {
            ret = parent->inode->ops->create(parent->inode, name, type, &inode);
            break;
        }
        case FS_NODE_OP_MKNOD: {
            if (parent->inode->ops->mknod) {
                ret = parent->inode->ops->mknod(parent->inode, name, type, priv,
                                                &inode);
            } else {
                ret = parent->inode->ops->create(parent->inode, name, type,
                                                 &inode);
            }
            break;
        }
    }
    if (ret != 0) {
        goto _exit;
    }

    /* Check callback result */
    if ((!inode) || (inode->type != type) || inode->sb) {
        ret = -MR_EINVAL;
        goto _exit;
    }

    /* Transfer the super-block reference to the inode */
    inode->sb = sb;
    sb = MR_NULL;

    /* Commit the reserved dentry */
    ret = mr_dentry_instantiate(child, inode);
    if (ret != 0) {
        goto _exit;
    }

    /* Dentry now owns the inode lifetime */
    mr_inode_put(inode);
    inode = MR_NULL;

    /* Get dentry */
    *dentry = mr_dentry_get(child);
    return 0;
_exit:
    /* Release inode */
    if (inode) {
        mr_inode_put(inode);
    }

    /* Release reservation */
    mr_dentry_del(child);
    if (sb) {
        mr_sblock_put(sb);
    }
    return ret;
}

static mr_err_t fs_create_node(mr_dentry_t *parent, const char *name,
                               mr_inode_type_t type, fs_node_op_t op,
                               void *priv, mr_dentry_t **dentry) {
    mr_sblock_t *sb;
    mr_err_t ret;

    /* Protect the parent FS across callback and dentry commit */
    ret = fs_op_enter_by_dentry(parent, &sb);
    if (ret != 0) {
        return ret;
    }
    ret = fs_create_node_op(parent, name, type, op, priv, dentry);
    fs_op_exit(sb);
    return ret;
}

static mr_err_t fs_create_path(mr_fs_struct_t *fs_struct, const char *path,
                               mr_inode_type_t type, fs_node_op_t op,
                               void *priv) {
    mr_dentry_t *parent, *dentry;
    fs_path_split_t split;
    mr_err_t ret;

    /* Split path into parent + leaf */
    ret = fs_split_path(path, &split);
    if (ret != 0) {
        return ret;
    }

    /* Look up the parent directory */
    ret = fs_lookup(fs_struct, split.parent, &parent, MR_NULL, MR_NULL);
    if (ret != 0) {
        fs_split_path_free(&split);
        return ret;
    }

    /* Create the node */
    ret = fs_create_node(parent, split.leaf, type, op, priv, &dentry);
    mr_dentry_put(parent);
    fs_split_path_free(&split);
    if (ret == 0) {
        mr_dentry_put(dentry);
    }
    return ret;
}

mr_err_t mr_fs_mkdir(const char *path) {
    mr_fs_struct_t *fs_struct;

    /* Check parameter */
    MR_ASSERT((path != MR_NULL) && (path[0] != '\0'));

    /* Get current file-system structure */
    fs_struct = mr_fs_struct_current();
    if (!fs_struct) {
        return -MR_EINVAL;
    }

    /* Create the directory node */
    return fs_create_path(fs_struct, path, MR_INODE_DIR, FS_NODE_OP_CREATE,
                          MR_NULL);
}

mr_err_t mr_fs_mknod(const char *path, mr_inode_type_t type, void *priv) {
    mr_fs_struct_t *fs_struct;

    /* Check parameter */
    MR_ASSERT((path != MR_NULL) && (path[0] != '\0'));

    /* Check node type */
    if ((type <= MR_INODE_NONE) || (type >= MR_INODE_MAX)) {
        return -MR_EINVAL;
    }

    /* Get current file-system structure */
    fs_struct = mr_fs_struct_current();
    if (!fs_struct) {
        return -MR_EINVAL;
    }

    /* Create the node */
    return fs_create_path(fs_struct, path, type, FS_NODE_OP_MKNOD, priv);
}

mr_err_t mr_fs_mkfifo(const char *path) {
    /* Create FIFO through mknod; capacity comes from the default */
    return mr_fs_mknod(path, MR_INODE_FIFO, MR_NULL);
}

static mr_err_t fs_file_close(mr_file_t *file) {
    mr_err_t ret, enter_ret;
    mr_sblock_t *sb;

    /* The inode pins the super-block throughout backend finalization */
    sb = file->inode ? file->inode->sb : MR_NULL;
    enter_ret = mr_sblock_op_enter(sb);
    if (enter_ret == 0) {
        /* Close and finalize the live backend handle */
        if (file->fops && file->fops->close) {
            ret = file->fops->close(file);
        } else {
            ret = 0;
        }
        mr_sblock_op_exit(sb);
    } else if (enter_ret == (-MR_ENODEV)) {
        /* Let close release per-file memory after media loss. The backend
         * must tolerate device errors and release memory regardless */
        if ((file->fops) && (file->fops->close)) {
            file->fops->close(file);
        }
        ret = -MR_ENODEV;
    } else {
        /* Still drop VFS references for an invalidated handle */
        ret = enter_ret;
    }

    /* Release the open-file lifetime before dropping the inode pin */
    mr_sblock_file_release(sb);

    /* Drop the references pinned at open time */
    if (file->inode) {
        mr_inode_put(file->inode);
        file->inode = MR_NULL;
    }
    if (file->dentry) {
        mr_dentry_put(file->dentry);
        file->dentry = MR_NULL;
    }

    /* Reset the file */
    file->fops = MR_NULL;
    file->pos = 0;
    file->flags = 0;
    file->priv = MR_NULL;
    return ret;
}

static mr_err_t fs_file_open(mr_file_t *file, mr_dentry_t *dentry,
                             mr_inode_t *inode, int flags) {
    mr_bool_t trunc;
    mr_err_t ret;

    /* O_TRUNC applies to a writable regular file only. Fail fast when the FS
     * can not truncate, before any state is touched */
    trunc = ((flags & MR_O_TRUNC) != 0) && (inode->type == MR_INODE_REG)
         && ((flags & MR_O_ACCMODE) != MR_O_RDONLY);
    if (trunc && ((!inode->ops) || (!inode->ops->truncate))) {
        return -MR_ENOSYS;
    }

    /* Pin the dentry/inode for the open file's lifetime. Caller drops its
     * own dentry reference on return, so we take our own here */
    file->dentry = mr_dentry_get(dentry);
    if (!file->dentry) {
        return -MR_ENOENT;
    }
    file->inode = mr_inode_get(inode);
    if (!file->inode) {
        mr_dentry_put(file->dentry);
        file->dentry = MR_NULL;
        return -MR_ENOENT;
    }

    /* Fill the file */
    file->fops = inode->fops;
    file->flags = flags;
    file->pos = 0;
    file->priv = MR_NULL;

    /* Count the open handle before entering the backend. The surrounding
     * operation prevents graceful unmount from racing this acquisition */
    ret = mr_sblock_file_acquire(inode->sb);
    if (ret != 0) {
        goto _exit_refs;
    }

    /* Open the file before any truncation, so an open failure never loses
     * data */
    if (inode->fops && inode->fops->open) {
        ret = inode->fops->open(inode, file);
        if (ret != 0) {
            file->fops = MR_NULL;
            goto _exit;
        }
    }

    /* O_TRUNC: now that the file is really open, shrink it to zero length */
    if (trunc) {
        ret = inode->ops->truncate(inode, file, 0);
        if (ret != 0) {
            goto _exit;
        }
    }

    /* Media detach may have made the mount permanently OFFLINE while the
     * backend callback was running */
    ret = mr_sblock_op_enter(inode->sb);
    if (ret != 0) {
        goto _exit;
    }
    mr_sblock_op_exit(inode->sb);
    return 0;
_exit:
    /* Close file */
    fs_file_close(file);
    return ret;
_exit_refs:
    /* Drop pins acquired before the open-file lifetime */
    mr_inode_put(file->inode);
    mr_dentry_put(file->dentry);
    file->inode = MR_NULL;
    file->dentry = MR_NULL;
    file->fops = MR_NULL;
    file->pos = 0;
    file->flags = 0;
    file->priv = MR_NULL;
    return ret;
}

static mr_err_t fs_open(mr_file_t *file, mr_fs_struct_t *fs_struct,
                        const char *path, int flags) {
    mr_dentry_t *dentry, *parent;
    mr_inode_t *inode;
    mr_sblock_t *sb;
    mr_err_t ret;
    char *name;

    /* File must not already be open */
    if (file->inode) {
        return -MR_EBUSY;
    }

    /* Validate the access mode (only RDONLY / WRONLY / RDWR) */
    if ((flags & MR_O_ACCMODE) > MR_O_RDWR) {
        return -MR_EINVAL;
    }

    /* Try to look up the existing node */
    ret = fs_lookup(fs_struct, path, &dentry,
                    (flags & MR_O_CREAT) ? &parent : MR_NULL,
                    (flags & MR_O_CREAT) ? &name : MR_NULL);
    if (ret != 0) {
        if (ret != (-MR_ENOENT)) {
            return ret;
        }

        /* Not found: create it if requested */
        if (!(flags & MR_O_CREAT)) {
            return -MR_ENOENT;
        }

        /* O_CREAT only creates regular files, so O_CREAT|O_DIRECTORY can not
         * yield a directory */
        if (flags & MR_O_DIRECTORY) {
            ret = -MR_ENOTDIR;
            goto _exit_create;
        }

        /* Only the last component can be created */
        if ((!parent) || (!name)) {
            ret = -MR_ENOENT;
            goto _exit_create;
        }

        /* Create the resolved last component */
        ret = fs_create_node(parent, name, MR_INODE_REG, FS_NODE_OP_CREATE,
                             MR_NULL, &dentry);
_exit_create:
        mr_free(name);
        mr_dentry_put(parent);
        if (ret != 0) {
            return ret;
        }
    } else {
        /* O_CREAT|O_EXCL: the file must not already exist */
        if ((flags & MR_O_CREAT) && (flags & MR_O_EXCL)) {
            mr_dentry_put(dentry);
            return -MR_EEXIST;
        }
    }

    /* Protect the resolved FS across validation and open callback */
    ret = fs_op_enter_by_dentry(dentry, &sb);
    if (ret != 0) {
        mr_dentry_put(dentry);
        return ret;
    }

    /* Node must have an inode */
    inode = dentry->inode;
    if (!inode) {
        ret = -MR_ENOENT;
        goto _exit;
    }

    /* A directory can only be opened read-only */
    if ((inode->type == MR_INODE_DIR)
        && ((flags & MR_O_ACCMODE) != MR_O_RDONLY)) {
        ret = -MR_EISDIR;
        goto _exit;
    }

    /* O_DIRECTORY: the node must be a directory */
    if ((flags & MR_O_DIRECTORY) && (inode->type != MR_INODE_DIR)) {
        ret = -MR_ENOTDIR;
        goto _exit;
    }

    /* Open file */
    ret = fs_file_open(file, dentry, inode, flags);
_exit:
    fs_op_exit(sb);
    mr_dentry_put(dentry);
    return ret;
}

mr_err_t mr_fs_open(mr_file_t *file, const char *path, int flags) {
    mr_fs_struct_t *fs_struct;

    /* Check parameter */
    MR_ASSERT((path != MR_NULL) && (path[0] != '\0'));
    MR_ASSERT(file != MR_NULL);

    /* Get current file-system structure */
    fs_struct = mr_fs_struct_current();
    if (!fs_struct) {
        return -MR_EINVAL;
    }

    /* Open relative to CWD */
    return fs_open(file, fs_struct, path, flags);
}

mr_err_t mr_fs_close(mr_file_t *file) {
    /* Check parameter */
    MR_ASSERT(file != MR_NULL);

    /* File must be open */
    if (!file->inode) {
        return -MR_EBADF;
    }

    /* Close file */
    return fs_file_close(file);
}

static mr_err_t fs_dentry_readdir(mr_file_t *file, mr_dirent_t *dirent) {
    mr_size_t index, len;
    mr_dentry_t *child;
    int mask;

    /* Get child dentry */
    if ((file->pos > 0)
        && ((mr_uint64_t)file->pos > (mr_uint64_t)MR_SIZE_MAX)) {
        return 0;
    }
    if (file->pos < 0) {
        return 0;
    }
    index = (mr_size_t)file->pos;
    child = mr_dentry_child_get(file->dentry, index);
    if (!child) {
        return 0;
    }

    /* Snapshot the name before a concurrent rename can replace its buffer */
    mask = mr_fs_struct_lock();
    len = MR_MIN(MR_DENTRY_NAME_LEN(child), MR_CFG_FS_PATH_LEN - 1U);
    mr_memcpy(dirent->name, MR_DENTRY_NAME(child), len);
    dirent->name[len] = '\0';
    dirent->type = child->inode ? child->inode->type : MR_INODE_NONE;
    mr_fs_struct_unlock(mask);
    mr_dentry_put(child);

    /* Update position */
    file->pos = (mr_int64_t)(index + 1);
    return 1;
}

mr_err_t mr_fs_readdir(mr_file_t *file, mr_dirent_t *dirent) {
    mr_sblock_t *sb;
    mr_err_t ret;

    /* Check parameter */
    MR_ASSERT(dirent != MR_NULL);
    MR_ASSERT(file != MR_NULL);

    /* File must be open */
    if (!file->inode) {
        return -MR_EBADF;
    }

    /* Only a directory can be read */
    if (file->inode->type != MR_INODE_DIR) {
        return -MR_ENOTDIR;
    }

    /* A reused mount-point must not expose another file system */
    if ((!file->dentry) || (file->dentry->inode != file->inode)) {
        return -MR_ENOENT;
    }

    /* Protect the backing FS across directory iteration */
    ret = fs_op_enter_by_file(file, &sb);
    if (ret != 0) {
        return ret;
    }

    /* Read the next entry */
    if (file->fops && file->fops->readdir) {
        ret = file->fops->readdir(file, dirent);
    } else {
        ret = fs_dentry_readdir(file, dirent);
    }
    fs_op_exit(sb);
    return ret;
}

mr_err_t mr_fs_fstat(const mr_file_t *file, mr_stat_t *stat) {
    mr_sblock_t *sb;
    mr_err_t ret;

    /* Check parameter */
    MR_ASSERT(file != MR_NULL);
    MR_ASSERT(stat != MR_NULL);

    /* File must be open */
    if (!file->inode) {
        return -MR_EBADF;
    }

    /* Protect the backing FS while reading status */
    ret = fs_op_enter_by_file(file, &sb);
    if (ret != 0) {
        return ret;
    }

    /* Fill generic inode status */
    stat->type = file->inode->type;
    stat->mode = file->inode->mode;
    stat->size = file->inode->size;

    /* Let the FS override dynamic fields if it provides a hook */
    if ((file->inode->ops) && (file->inode->ops->stat)) {
        ret = file->inode->ops->stat(file->inode, stat);
    } else {
        ret = 0;
    }
    fs_op_exit(sb);
    return ret;
}

static mr_bool_t fs_dentry_is_mount(mr_dentry_t *dentry) {
    mr_sblock_t *sb;

    /* Find either side of a mount boundary */
    sb = mr_sblock_find_by_dentry(dentry);
    if (!sb) {
        return MR_FALSE;
    }
    mr_sblock_put(sb);
    return MR_TRUE;
}

static mr_err_t fs_rename_dentry(mr_dentry_t *old_dentry, mr_inode_t *inode,
                                 mr_fs_struct_t *fs_struct,
                                 const char *new_path) {
    mr_dentry_t *old_parent, *new_parent;
    mr_sblock_t *target_guard;
    fs_path_split_t split;
    mr_err_t ret;
    int mask;

    /* Dentry must still refer to the inode */
    if ((!old_dentry) || (old_dentry->inode != inode)) {
        return -MR_ENOENT;
    }

    /* A mount-point directory can not be renamed */
    if (fs_dentry_is_mount(old_dentry)) {
        return -MR_EBUSY;
    }

    /* Pin the source parent while the binding is stable. A concurrent move
     * can not retire the pinned object underneath the checks below */
    mask = mr_fs_struct_lock();
    old_parent = old_dentry->upper ? mr_dentry_get(old_dentry->upper) : MR_NULL;
    mr_fs_struct_unlock(mask);

    /* Source must have a directory parent */
    if (!old_parent) {
        return -MR_ENOENT;
    }

    /* Source parent must be a directory */
    if ((!old_parent->inode) || (old_parent->inode->type != MR_INODE_DIR)) {
        ret = -MR_ENOTDIR;
        goto _exit_parent;
    }

    /* Split the destination into parent + leaf */
    ret = fs_split_path(new_path, &split);
    if (ret != 0) {
        goto _exit_parent;
    }

    /* Look up the destination parent directory */
    ret = fs_lookup(fs_struct, split.parent, &new_parent, MR_NULL, MR_NULL);
    if (ret != 0) {
        goto _exit_free;
    }
    target_guard = MR_NULL;
    ret = fs_op_enter_by_dentry(new_parent, &target_guard);
    if (ret != 0) {
        goto _exit_put;
    }
    if (!new_parent->inode) {
        ret = -MR_ENOENT;
        goto _exit_put;
    }

    /* Destination parent must be a directory */
    if (new_parent->inode->type != MR_INODE_DIR) {
        ret = -MR_ENOTDIR;
        goto _exit_put;
    }

    /* Prepare checks the super-block boundary under the structure lock */
    ret = mr_dentry_rename(old_dentry, new_parent, split.leaf);
_exit_put:
    if (target_guard) {
        fs_op_exit(target_guard);
    }
    mr_dentry_put(new_parent);
_exit_free:
    fs_split_path_free(&split);
_exit_parent:
    mr_dentry_put(old_parent);
    return ret;
}

static mr_err_t fs_unlink_dentry(mr_dentry_t *dentry, mr_inode_t *inode) {
    /* Dentry must still refer to the inode */
    if ((!dentry) || (dentry->inode != inode)) {
        return -MR_ENOENT;
    }

    /* A mount-point directory can not be removed */
    if (fs_dentry_is_mount(dentry)) {
        return -MR_EBUSY;
    }

    /* Validate the parent and children under the unlink reservation lock */
    return mr_dentry_unlink(dentry);
}

mr_err_t mr_fs_rmdir(const char *path) {
    mr_fs_struct_t *fs_struct;
    mr_dentry_t *dentry;
    mr_sblock_t *sb;
    mr_err_t ret;

    /* Check parameter */
    MR_ASSERT((path != MR_NULL) && (path[0] != '\0'));

    /* Get current file-system structure */
    fs_struct = mr_fs_struct_current();
    if (!fs_struct) {
        return -MR_EINVAL;
    }

    /* Look up the directory itself */
    ret = fs_lookup(fs_struct, path, &dentry, MR_NULL, MR_NULL);
    if (ret != 0) {
        return ret;
    }

    /* Only directories can be removed by rmdir */
    if ((!dentry->inode) || (dentry->inode->type != MR_INODE_DIR)) {
        mr_dentry_put(dentry);
        return -MR_ENOTDIR;
    }

    /* Protect the backing FS across unlink commit */
    ret = fs_op_enter_by_dentry(dentry, &sb);
    if (ret == 0) {
        ret = fs_unlink_dentry(dentry, dentry->inode);
        fs_op_exit(sb);
    }
    mr_dentry_put(dentry);
    return ret;
}

mr_err_t mr_fs_rename(const char *old_path, const char *new_path) {
    mr_fs_struct_t *fs_struct;
    mr_dentry_t *dentry;
    mr_sblock_t *sb;
    mr_err_t ret;

    /* Check parameter */
    MR_ASSERT((old_path != MR_NULL) && (old_path[0] != '\0'));
    MR_ASSERT((new_path != MR_NULL) && (new_path[0] != '\0'));

    /* Get current file-system structure */
    fs_struct = mr_fs_struct_current();
    if (!fs_struct) {
        return -MR_EINVAL;
    }

    /* Look up the source name */
    ret = fs_lookup(fs_struct, old_path, &dentry, MR_NULL, MR_NULL);
    if (ret != 0) {
        return ret;
    }

    /* Protect source and destination FS across rename commit */
    ret = fs_op_enter_by_dentry(dentry, &sb);
    if (ret == 0) {
        ret = fs_rename_dentry(dentry, dentry->inode, fs_struct, new_path);
        fs_op_exit(sb);
    }
    mr_dentry_put(dentry);
    return ret;
}

mr_err_t mr_fs_unlink(const char *path) {
    mr_fs_struct_t *fs_struct;
    mr_dentry_t *dentry;
    mr_sblock_t *sb;
    mr_err_t ret;

    /* Check parameter */
    MR_ASSERT((path != MR_NULL) && (path[0] != '\0'));

    /* Get current file-system structure */
    fs_struct = mr_fs_struct_current();
    if (!fs_struct) {
        return -MR_EINVAL;
    }

    /* Look up the name */
    ret = fs_lookup(fs_struct, path, &dentry, MR_NULL, MR_NULL);
    if (ret != 0) {
        return ret;
    }

    /* Directories are removed through rmdir */
    if (dentry->inode && (dentry->inode->type == MR_INODE_DIR)) {
        mr_dentry_put(dentry);
        return -MR_EISDIR;
    }

    /* Protect the backing FS across unlink commit */
    ret = fs_op_enter_by_dentry(dentry, &sb);
    if (ret == 0) {
        ret = fs_unlink_dentry(dentry, dentry->inode);
        fs_op_exit(sb);
    }
    mr_dentry_put(dentry);
    return ret;
}

mr_ssize_t mr_fs_read(mr_file_t *file, void *buf, mr_size_t size) {
    mr_sblock_t *sb;
    mr_ssize_t ret;
    mr_err_t guard;

    /* Check parameter */
    MR_ASSERT((buf != MR_NULL) || (size == 0));
    MR_ASSERT(file != MR_NULL);

    /* File must be open */
    if (!file->inode) {
        return -MR_EBADF;
    }

    /* File must not be write-only */
    if ((file->flags & MR_O_ACCMODE) == MR_O_WRONLY) {
        return -MR_EBADF;
    }

    /* Directory cannot be read */
    if (file->inode->type == MR_INODE_DIR) {
        return -MR_EISDIR;
    }

    /* Check the FS supports reading */
    if ((!file->fops) || (!file->fops->read)) {
        return -MR_ENOSYS;
    }

    /* Protect the backing FS across the read */
    guard = fs_op_enter_by_file(file, &sb);
    if (guard != 0) {
        return (mr_ssize_t)guard;
    }

    /* Read from the file */
    ret = file->fops->read(file, buf, size);
    fs_op_exit(sb);
    return ret;
}

mr_ssize_t mr_fs_write(mr_file_t *file, const void *buf, mr_size_t size) {
    mr_sblock_t *sb;
    mr_ssize_t ret;
    mr_err_t guard;

    /* Check parameter */
    MR_ASSERT((buf != MR_NULL) || (size == 0));
    MR_ASSERT(file != MR_NULL);

    /* File must be open */
    if (!file->inode) {
        return -MR_EBADF;
    }

    /* File must not be read-only */
    if ((file->flags & MR_O_ACCMODE) == MR_O_RDONLY) {
        return -MR_EBADF;
    }

    /* Directory cannot be written */
    if (file->inode->type == MR_INODE_DIR) {
        return -MR_EISDIR;
    }

    /* Check the FS supports writing */
    if ((!file->fops) || (!file->fops->write)) {
        return -MR_ENOSYS;
    }

    /* Protect the backing FS across the write. O_APPEND is applied by the
     * write callback under its own data lock */
    guard = fs_op_enter_by_file(file, &sb);
    if (guard != 0) {
        return (mr_ssize_t)guard;
    }

    /* Write to the file */
    ret = file->fops->write(file, buf, size);
    fs_op_exit(sb);
    return ret;
}

mr_int64_t mr_fs_lseek(mr_file_t *file, mr_int64_t offset, int whence) {
    mr_int64_t position;
    mr_sblock_t *sb;
    mr_int64_t base;
    mr_err_t ret;

    /* Check parameter */
    MR_ASSERT(file != MR_NULL);

    /* File must be open */
    if (!file->inode) {
        return -MR_EBADF;
    }

    /* Protect the backing FS across the seek */
    ret = fs_op_enter_by_file(file, &sb);
    if (ret != 0) {
        return (mr_int64_t)ret;
    }

    /* Seek the file */
    if (file->fops && file->fops->llseek) {
        position = file->fops->llseek(file, offset, whence);
        goto _exit;
    }

    /* Compute the base */
    switch (whence) {
        case MR_SEEK_SET: {
            base = 0;
            break;
        }
        case MR_SEEK_CUR: {
            base = file->pos;
            break;
        }
        case MR_SEEK_END: {
            base = file->inode->size;
            break;
        }
        default: {
            position = -MR_EINVAL;
            goto _exit;
        }
    }

    /* Check range before adding signed values */
    if (((offset < 0) && (base < ((-MR_INT64_MAX - 1) - offset)))
        || ((offset > 0) && (base > (MR_INT64_MAX - offset)))) {
        position = -MR_ERANGE;
        goto _exit;
    }

    /* Set position */
    position = base + offset;
    if (position < 0) {
        position = -MR_EINVAL;
        goto _exit;
    }
    file->pos = position;
_exit:
    fs_op_exit(sb);
    return position;
}

mr_err_t mr_fs_ftruncate(mr_file_t *file, mr_int64_t length) {
    mr_sblock_t *sb;
    mr_err_t ret;

    /* Check parameter */
    MR_ASSERT(file != MR_NULL);

    /* File must be open */
    if (!file->inode) {
        return -MR_EBADF;
    }

    /* Truncation applies to regular files only */
    if (file->inode->type == MR_INODE_DIR) {
        return -MR_EISDIR;
    }
    if (file->inode->type != MR_INODE_REG) {
        return -MR_EINVAL;
    }

    /* File must be open for writing */
    if ((file->flags & MR_O_ACCMODE) == MR_O_RDONLY) {
        return -MR_EBADF;
    }

    /* Length must be valid */
    if (length < 0) {
        return -MR_EINVAL;
    }

    /* Check the FS supports truncation */
    if ((!file->inode->ops) || (!file->inode->ops->truncate)) {
        return -MR_ENOSYS;
    }

    /* Protect the backing FS across truncation */
    ret = fs_op_enter_by_file(file, &sb);
    if (ret != 0) {
        return ret;
    }

    /* Truncate the file */
    ret = file->inode->ops->truncate(file->inode, file, length);
    fs_op_exit(sb);
    return ret;
}

mr_err_t mr_fs_fsync(mr_file_t *file) {
    mr_sblock_t *sb;
    mr_err_t ret;

    /* Check parameter */
    MR_ASSERT(file != MR_NULL);

    /* File must be open */
    if (!file->inode) {
        return -MR_EBADF;
    }

    /* Check the FS supports sync */
    if ((!file->fops) || (!file->fops->fsync)) {
        return -MR_ENOSYS;
    }

    /* Protect the backing FS across sync */
    ret = fs_op_enter_by_file(file, &sb);
    if (ret != 0) {
        return ret;
    }

    /* Sync the file */
    ret = file->fops->fsync(file);
    fs_op_exit(sb);
    return ret;
}

mr_err_t mr_fs_ioctl(mr_file_t *file, int cmd, void *arg) {
    mr_sblock_t *sb;
    mr_err_t ret;

    /* Check parameter */
    MR_ASSERT(file != MR_NULL);

    /* File must be open */
    if (!file->inode) {
        return -MR_EBADF;
    }

    /* Check the FS supports ioctl */
    if ((!file->fops) || (!file->fops->ioctl)) {
        return -MR_ENOSYS;
    }

    /* Protect the backing FS across ioctl */
    ret = fs_op_enter_by_file(file, &sb);
    if (ret != 0) {
        return ret;
    }

    /* Control the file */
    ret = file->fops->ioctl(file, cmd, arg);
    fs_op_exit(sb);
    return ret;
}

mr_err_t mr_fs_chroot(const char *path) {
    mr_dentry_t *dentry, *cwd, *old_root, *old_cwd;
    mr_fs_struct_t *fs_struct;
    mr_inode_t *inode;
    mr_sblock_t *sb;
    mr_err_t ret;
    int mask;

    /* Check parameter */
    MR_ASSERT((path != MR_NULL) && (path[0] != '\0'));

    /* Get current file-system structure */
    fs_struct = mr_fs_struct_current();
    if (!fs_struct) {
        return -MR_EINVAL;
    }

    /* Look up the new root */
    ret = fs_lookup(fs_struct, path, &dentry, MR_NULL, MR_NULL);
    if (ret != 0) {
        return ret;
    }

    /* Protect the new root while updating structure context */
    ret = fs_op_enter_by_dentry(dentry, &sb);
    if (ret != 0) {
        mr_dentry_put(dentry);
        return ret;
    }

    /* Lock */
    mask = mr_fs_struct_lock();

    /* New root must still be a directory */
    inode = dentry->inode;
    if ((!inode) || (inode->type != MR_INODE_DIR)) {
        /* Unlock */
        mr_fs_struct_unlock(mask);
        fs_op_exit(sb);
        mr_dentry_put(dentry);
        return -MR_ENOTDIR;
    }

    /* Keep CWD only when it is contained by the new root */
    for (cwd = fs_struct->cwd; cwd; cwd = cwd->upper) {
        if (cwd == dentry) {
            cwd = fs_struct->cwd;
            break;
        }
    }

    /* Reset an external CWD to the new root */
    if (!cwd) {
        cwd = mr_dentry_get(dentry);
        if (!cwd) {
            /* Unlock */
            mr_fs_struct_unlock(mask);
            fs_op_exit(sb);
            mr_dentry_put(dentry);
            return -MR_EINVAL;
        }
    }

    /* Swap the context root */
    old_root = fs_struct->root;
    old_cwd = fs_struct->cwd;
    fs_struct->root = dentry;
    fs_struct->cwd = cwd;

    /* Unlock */
    mr_fs_struct_unlock(mask);
    fs_op_exit(sb);

    /* Put the old context */
    mr_dentry_put(old_root);
    if (old_cwd != cwd) {
        mr_dentry_put(old_cwd);
    }
    return 0;
}

mr_err_t mr_fs_chdir(const char *path) {
    mr_dentry_t *dentry, *old_cwd;
    mr_fs_struct_t *fs_struct;
    mr_inode_t *inode;
    mr_sblock_t *sb;
    mr_err_t ret;
    int mask;

    /* Check parameter */
    MR_ASSERT((path != MR_NULL) && (path[0] != '\0'));

    /* Get current file-system structure */
    fs_struct = mr_fs_struct_current();
    if (!fs_struct) {
        return -MR_EINVAL;
    }

    /* Look up the new working directory */
    ret = fs_lookup(fs_struct, path, &dentry, MR_NULL, MR_NULL);
    if (ret != 0) {
        return ret;
    }

    /* Protect the new CWD while updating structure context */
    ret = fs_op_enter_by_dentry(dentry, &sb);
    if (ret != 0) {
        mr_dentry_put(dentry);
        return ret;
    }

    /* Lock */
    mask = mr_fs_struct_lock();

    /* New working directory must still be a directory */
    inode = dentry->inode;
    if ((!inode) || (inode->type != MR_INODE_DIR)) {
        /* Unlock */
        mr_fs_struct_unlock(mask);
        fs_op_exit(sb);
        mr_dentry_put(dentry);
        return -MR_ENOTDIR;
    }

    /* Swap the context CWD */
    old_cwd = fs_struct->cwd;
    fs_struct->cwd = dentry;

    /* Unlock */
    mr_fs_struct_unlock(mask);
    fs_op_exit(sb);

    /* Put old CWD */
    mr_dentry_put(old_cwd);
    return 0;
}

mr_ssize_t mr_fs_getcwd(char *buf, mr_size_t size) {
    mr_size_t len, pos, name_len;
    mr_fs_struct_t *fs_struct;
    mr_dentry_t *dentry;
    mr_ssize_t ret;
    int mask;

    /* Check parameter */
    MR_ASSERT((buf != MR_NULL) && (size > 0));

    /* Get current file-system structure */
    fs_struct = mr_fs_struct_current();
    if (!fs_struct) {
        return -MR_EINVAL;
    }

    /* Lock */
    mask = mr_fs_struct_lock();

    /* Check structure context */
    if ((!fs_struct->root) || (!fs_struct->cwd)) {
        ret = -MR_EINVAL;
        goto _exit;
    }

    /* CWD is the root */
    dentry = fs_struct->cwd;
    if (dentry == fs_struct->root) {
        if (size < 2) {
            ret = -MR_ERANGE;
            goto _exit;
        }
        buf[0] = '/';
        buf[1] = '\0';
        ret = 1;
        goto _exit;
    }

    /* Compute the path length(excluding '\0') */
    len = 0;
    while (dentry != fs_struct->root) {
        if (!dentry->upper) {
            /* CWD is not under the root */
            ret = -MR_ENOENT;
            goto _exit;
        }
        name_len = MR_DENTRY_NAME_LEN(dentry);
        if ((len > (MR_SIZE_MAX - name_len - 1U))
            || (name_len == MR_SIZE_MAX)) {
            ret = -MR_ERANGE;
            goto _exit;
        }
        len += name_len + 1U;
        dentry = dentry->upper;
    }

    /* Check the buffer can hold the path and the terminator */
    if ((len > (mr_size_t)MR_SSIZE_MAX) || ((len + 1U) > size)) {
        ret = -MR_ERANGE;
        goto _exit;
    }

    /* Write the path backwards from the end */
    dentry = fs_struct->cwd;
    pos = len;
    buf[pos] = '\0';
    while (dentry != fs_struct->root) {
        name_len = MR_DENTRY_NAME_LEN(dentry);
        pos -= name_len;
        mr_strncpy(&buf[pos], MR_DENTRY_NAME(dentry), name_len);
        pos -= 1U;
        buf[pos] = '/';
        dentry = dentry->upper;
    }
    ret = (mr_ssize_t)len;
_exit:
    /* Unlock */
    mr_fs_struct_unlock(mask);
    return ret;
}

void mr_fs_file_init(void) {
#if !defined(MR_USE_FS_MULTIUSER)
    /* Init single user FS structure */
    mr_fs_struct_init(mr_fs_struct_current());
#endif /* !defined(MR_USE_FS_MULTIUSER) */
}
#endif /* defined(MR_USE_FS) */
