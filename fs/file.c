/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/fs/mr_file.h>
#if defined(MR_USE_FS)
#include <mrX/libc/mr_malloc.h>
#include <mrX/libc/mr_string.h>
#include <mrX/fs/mr_nsp.h>

/* Built-in symbolic-link operations */
static mr_ssize_t fs_symlink_readlink(mr_inode_t *inode, char *buf,
                                      mr_size_t size);
static void fs_symlink_destroy(mr_inode_t *inode);
static const mr_inode_ops_t __fs_symlink_ops = {
    .readlink = fs_symlink_readlink,
    .destroy = fs_symlink_destroy,
};

/* FS node creation operation */
typedef enum fs_node_create_op {
    FS_NODE_OP_CREATE,
    FS_NODE_OP_MKNOD,
} fs_node_create_op_t;

/* FS path split type */
typedef struct fs_path_split {
    char ipath[MR_CFG_FS_DENTRY_NAME_LEN * 2];
    const char *leaf;
    char *parent;
} fs_path_split_t;

/* Directory-relative path context */
typedef struct fs_at_context {
    mr_super_block_t *sb;
    mr_fs_nsp_t nsp;
} fs_at_context_t;

static mr_ssize_t fs_symlink_readlink(mr_inode_t *inode, char *buf,
                                      mr_size_t size) {
    mr_size_t len;

    /* Copy target without a terminator */
    len = (mr_size_t)inode->size;
    if (size < len) {
        len = size;
    }
    mr_memcpy(buf, inode->priv, len);
    return (mr_ssize_t)len;
}

static void fs_symlink_destroy(mr_inode_t *inode) {
    /* Free symbolic-link target */
    mr_free(inode->priv);
}

static mr_err_t fs_path_join(const char *target, const char *rest,
                             char **path) {
    mr_size_t target_len, rest_len, len;
    mr_bool_t separator;
    char *joined;

    /* Compute joined path length */
    target_len = mr_strlen(target);
    rest_len = mr_strlen(rest);
    separator = (rest_len > 0) && (target[target_len - 1U] != '/');
    if ((target_len > (MR_SIZE_MAX - rest_len - (mr_size_t)separator))
        || ((target_len + rest_len + (mr_size_t)separator)
            >= MR_CFG_FS_PATH_LEN)) {
        return -MR_ENAMETOOLONG;
    }
    len = target_len + rest_len + (mr_size_t)separator;

    /* Join target and unresolved suffix */
    joined = mr_malloc(len + 1U);
    if (!joined) {
        return -MR_ENOMEM;
    }
    mr_strncpy(joined, target, target_len);
    if (separator) {
        joined[target_len] = '/';
        target_len++;
    }
    mr_strncpy(&joined[target_len], rest, rest_len);
    joined[len] = '\0';
    *path = joined;
    return 0;
}

static mr_err_t fs_nsp_snapshot(mr_fs_nsp_t *nsp, const char *path,
                                mr_dentry_t **start, mr_dentry_t **root) {
    int mask;

    /* Lock */
    mask = mr_fs_nsp_lock();

    /* Pin ROOT and lookup start while the namespace is stable */
    *start = mr_dentry_get((path[0] == '/') ? nsp->root : nsp->cwd);
    *root = mr_dentry_get(nsp->root);

    /* Unlock */
    mr_fs_nsp_unlock(mask);

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

static mr_err_t fs_inode_op_enter(mr_inode_t *inode, mr_super_block_t **out) {
    mr_super_block_t *sb;
    mr_err_t ret;

    /* Pin the super-block through the operation */
    sb = inode ? mr_super_block_get(inode->sb) : MR_NULL;
    if (!sb) {
        return -MR_ENOENT;
    }
    ret = mr_super_block_op_enter(sb);
    if (ret != 0) {
        mr_super_block_put(sb);
        return ret;
    }
    *out = sb;
    return 0;
}

static mr_err_t fs_dentry_op_enter(mr_dentry_t *dentry,
                                   mr_super_block_t **out) {
    mr_inode_t *inode;
    mr_err_t ret;
    int mask;

    /* Lock */
    mask = mr_fs_nsp_lock();

    /* Pin the inode while its dentry binding is stable */
    inode = (dentry && dentry->inode) ? mr_inode_get(dentry->inode) : MR_NULL;

    /* Unlock */
    mr_fs_nsp_unlock(mask);

    /* Check inode */
    if (!inode) {
        return -MR_ENOENT;
    }

    /* Enter the inode's super-block operation */
    ret = fs_inode_op_enter(inode, out);
    mr_inode_put(inode);
    return ret;
}

static void fs_op_exit(mr_super_block_t *sb) {
    /* Leave and release the super-block */
    mr_super_block_op_exit(sb);
    mr_super_block_put(sb);
}

static void fs_at_context_release(fs_at_context_t *context) {
    /* Release the pinned namespace context */
    if (context->nsp.root) {
        mr_dentry_put(context->nsp.root);
        context->nsp.root = MR_NULL;
    }
    if (context->nsp.cwd) {
        mr_dentry_put(context->nsp.cwd);
        context->nsp.cwd = MR_NULL;
    }
    if (context->sb) {
        fs_op_exit(context->sb);
        context->sb = MR_NULL;
    }
}

static mr_err_t fs_at_context_enter(fs_at_context_t *context,
                                    const mr_file_t *dir, const char *path,
                                    int flags) {
    mr_dentry_t *root, *cwd;
    mr_fs_nsp_t *nsp;
    mr_err_t ret;
    int mask;

    /* Init context */
    context->nsp.root = MR_NULL;
    context->nsp.cwd = MR_NULL;
    context->sb = MR_NULL;

    /* Get current file-system namespace */
    nsp = mr_fs_nsp_current();
    if (!nsp) {
        return -MR_EINVAL;
    }

    /* Absolute paths use the current context unless confined by the caller */
    if ((path[0] == '/') && !(flags & MR_AT_IN_ROOT)) {
        /* Lock */
        mask = mr_fs_nsp_lock();

        /* Pin current namespace context */
        context->nsp.root = nsp->root ? mr_dentry_get(nsp->root) : MR_NULL;
        context->nsp.cwd = nsp->cwd ? mr_dentry_get(nsp->cwd) : MR_NULL;

        /* Unlock */
        mr_fs_nsp_unlock(mask);
    } else {
        /* Base must be an open directory handle */
        if ((!dir) || (!dir->inode)) {
            return -MR_EBADF;
        }
        if (dir->inode->type != MR_INODE_DIR) {
            return -MR_ENOTDIR;
        }

        /* Protect the base directory FS through relative lookup */
        ret = fs_inode_op_enter(dir->inode, &context->sb);
        if (ret != 0) {
            return ret;
        }

        /* Lock */
        mask = mr_fs_nsp_lock();

        /* Handle must still refer to its original inode */
        if ((!dir->dentry) || (dir->dentry->inode != dir->inode)) {
            /* Unlock */
            mr_fs_nsp_unlock(mask);
            ret = -MR_ENOENT;
            goto _exit;
        }

        /* Select the lookup boundary and starting directory */
        root = (flags & MR_AT_IN_ROOT) ? dir->dentry : nsp->root;
        while ((flags & MR_AT_IN_ROOT) && root->mounted) {
            root = root->mounted;
        }
        cwd = dir->dentry;

        /* Pin directory-relative namespace context */
        context->nsp.root = root ? mr_dentry_get(root) : MR_NULL;
        context->nsp.cwd = mr_dentry_get(cwd);

        /* Unlock */
        mr_fs_nsp_unlock(mask);
    }

    /* Check the pinned context */
    if ((!context->nsp.root) || (!context->nsp.cwd)) {
        ret = -MR_ENOENT;
        goto _exit;
    }
    return 0;
_exit:
    fs_at_context_release(context);
    return ret;
}

static mr_err_t fs_readlink_target(mr_inode_t *inode, char **target) {
    mr_super_block_t *sb;
    mr_ssize_t ret;
    mr_size_t size;
    char *path;

    /* Check symbolic-link inode */
    if (inode->type != MR_INODE_LNK) {
        return -MR_EINVAL;
    }
    if ((!inode->ops) || (!inode->ops->readlink)) {
        return -MR_ENOSYS;
    }
    if ((inode->size <= 0)
        || ((mr_uint64_t)inode->size >= (mr_uint64_t)MR_CFG_FS_PATH_LEN)) {
        return -MR_ENAMETOOLONG;
    }
    size = (mr_size_t)inode->size;

    /* Protect the backing FS across readlink */
    ret = fs_inode_op_enter(inode, &sb);
    if (ret != 0) {
        return (mr_err_t)ret;
    }

    /* Read target */
    path = mr_malloc(size + 1U);
    if (!path) {
        fs_op_exit(sb);
        return -MR_ENOMEM;
    }
    ret = inode->ops->readlink(inode, path, size);
    fs_op_exit(sb);
    if ((ret <= 0) || ((mr_size_t)ret > size)) {
        mr_free(path);
        return (ret < 0) ? (mr_err_t)ret : -MR_EIO;
    }
    path[ret] = '\0';
    *target = path;
    return 0;
}

static mr_err_t fs_lookup(mr_fs_nsp_t *nsp, const char *path,
                          mr_bool_t follow_final, mr_dentry_t **result) {
    mr_dentry_t *current, *next, *base, *root;
    mr_size_t offset, begin, rest, len;
    char *pending, *target, *joined;
    mr_uint32_t links;
    mr_err_t ret;
    char saved;

    /* Init result */
    *result = MR_NULL;

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
    ret = fs_nsp_snapshot(nsp, path, &current, &root);
    if (ret != 0) {
        mr_free(pending);
        return ret;
    }

    /* Resolve every component and expand symbolic links */
    for (offset = 0, links = 0;;) {
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
            goto _exit_current;
        }

        /* Follow a symbolic link unless the final link is requested */
        if (next->inode && (next->inode->type == MR_INODE_LNK)
            && (follow_final || (pending[rest] != '\0'))) {
#if !defined(MR_CFG_FS_SYMLINK_MAX)
#define MR_CFG_FS_SYMLINK_MAX           (16U)
#endif /* !defined(MR_CFG_FS_SYMLINK_MAX) */
            if ((++links) > MR_CFG_FS_SYMLINK_MAX) {
                mr_dentry_put(next);
                ret = -MR_ELOOP;
                goto _exit_current;
            }

            /* Read and splice the target with the unresolved suffix */
            ret = fs_readlink_target(next->inode, &target);
            if (ret != 0) {
                mr_dentry_put(next);
                goto _exit_current;
            }
            ret = fs_path_join(target, &pending[rest], &joined);
            if (ret != 0) {
                mr_dentry_put(next);
                mr_free(target);
                goto _exit_current;
            }

            /* Absolute targets restart at ROOT; relative targets restart in
             * the directory containing the link */
            base = mr_dentry_get((target[0] == '/') ? root : current);
            mr_dentry_put(next);
            mr_free(target);
            if (!base) {
                mr_free(joined);
                ret = -MR_ENOENT;
                goto _exit_current;
            }
            mr_dentry_put(current);
            mr_free(pending);
            pending = joined;
            current = base;
            offset = 0;
            continue;
        }

        /* Continue from the resolved dentry */
        mr_dentry_put(current);
        current = next;
        offset = rest;
    }

    /* Return resolved dentry */
    mr_dentry_put(root);
    mr_free(pending);
    *result = current;
    return 0;
_exit_current:
    mr_dentry_put(current);
_exit:
    mr_dentry_put(root);
    mr_free(pending);
    return ret;
}

mr_err_t mr_fs_mount(const char *source, const char *path, const char *fstype,
                     int flags, void *data) {
    mr_super_block_t *sb, *guard;
    mr_super_block_t *existing;
    mr_dentry_t *mnt, *root;
    mr_file_system_t *fs;
    mr_bool_t root_mount;
    mr_fs_nsp_t *nsp;
    mr_err_t ret;

    /* Check parameter */
    MR_ASSERT((fstype != MR_NULL) && (fstype[0] != '\0'));
    MR_ASSERT((path != MR_NULL) && (path[0] != '\0'));

    /* Get current file-system namespace */
    nsp = mr_fs_nsp_current();
    if (!nsp) {
        return -MR_EINVAL;
    }

    /* Find FS */
    fs = mr_file_system_find(fstype);
    if (!fs) {
        return -MR_ENOENT;
    }

    /* Resolve the mount-point. Mount never creates namespace paths; treefs or
     * the mounted parent file-system provides the directory structure */
    ret = fs_lookup(nsp, path, MR_TRUE, &mnt);
    if (ret != 0) {
        return ret;
    }

    /* Protect the covered super-block while attaching a child mount. The
     * initial global ROOT has no super-block to protect */
    root_mount = mr_fs_nsp_is_root(mnt);
    guard = MR_NULL;
    ret = fs_dentry_op_enter(mnt, &guard);
    if ((ret != 0) && ((!root_mount) || (ret != (-MR_ENOENT)))) {
        goto _exit;
    }

    /* Reject a repeated mount and validate the covered node */
    existing = mr_super_block_find_by_dentry(mnt);
    if (existing) {
        mr_super_block_put(existing);
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

    /* Create and mount the super-block */
    ret = mr_super_block_create(fs, source, flags, data, &sb);
    if (ret != 0) {
        goto _exit;
    }

    /* Create a separate mounted ROOT for nested mounts. Global ROOT is
     * the only dentry mounted in place */
    if (!root_mount) {
        root = mr_dentry_create(sb->dops, "%s", MR_DENTRY_NAME(mnt));
        if (!root) {
            mr_super_block_del(sb);
            ret = -MR_ENOMEM;
            goto _exit;
        }
    } else {
        root = mnt;
    }

    /* Bind and attach the mounted ROOT */
    ret = mr_dentry_instantiate(root, sb->root);
    if (ret != 0) {
        if (!root_mount) {
            mr_dentry_del(root);
        }
        mr_super_block_del(sb);
        goto _exit;
    }
    ret = mr_super_block_attach(sb, mnt, root);
    if (ret != 0) {
        if (root_mount) {
            mr_dentry_uninstantiate(root);
        } else {
            mr_dentry_del(root);
        }
        mr_super_block_del(sb);
        goto _exit;
    }

    /* Super-block owns the mounted ROOT reference */
    if (!root_mount) {
        mr_dentry_put(root);
    }
    if (guard) {
        fs_op_exit(guard);
    }
    mr_dentry_put(mnt);
    return 0;
_exit:
    /* Put dentry */
    if (guard) {
        fs_op_exit(guard);
    }
    mr_dentry_put(mnt);
    return ret;
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
        mr_dentry_del(current);
        mr_dentry_put(current);
        current = parent;
    }
}

mr_err_t mr_fs_umount(const char *path) {
    mr_dentry_t *dentry, *mnt, *root;
    mr_super_block_t *sb;
    mr_fs_nsp_t *nsp;
    mr_err_t ret;

    /* Check parameter */
    MR_ASSERT((path != MR_NULL) && (path[0] != '\0'));

    /* Get current file-system namespace */
    nsp = mr_fs_nsp_current();
    if (!nsp) {
        return -MR_EINVAL;
    }

    /* Resolve the visible mounted ROOT */
    ret = fs_lookup(nsp, path, MR_TRUE, &dentry);
    if (ret != 0) {
        return ret;
    }

    /* Find the super-block mounted at this path */
    sb = mr_super_block_find_by_dentry(dentry);
    if (!sb) {
        mr_dentry_put(dentry);
        return -MR_ENOENT;
    }

    /* Reserve super-block detach before notifying the backing FS */
    ret = mr_super_block_detach_prepare(sb);
    if (ret != 0) {
        mr_super_block_put(sb);
        mr_dentry_put(dentry);
        return ret;
    }

    /* Let the backing FS flush or reject unmount */
    if (sb->type->unmount) {
        ret = sb->type->unmount(sb);
        if (ret != 0) {
            mr_super_block_detach_cancel(sb);
            mr_super_block_put(sb);
            mr_dentry_put(dentry);
            return ret;
        }
    }

    /* Atomically hide the mounted ROOT and detach its topology. A parent
     * mount with children returns -MR_EBUSY */
    ret = mr_super_block_detach(sb, &mnt, &root);
    if (ret != 0) {
        mr_super_block_detach_cancel(sb);
        mr_super_block_put(sb);
        mr_dentry_put(dentry);
        return ret;
    }

    /* Tear down the mounted subtree */
    fs_purge_children(root);

    /* Delete the super-block. This drops the ROOT inode's creation reference;
     * final kill_sb is deferred while open files keep inode/sb references */
    ret = mr_super_block_del(sb);
    mr_super_block_put(sb);

    /* Delete the separate mounted ROOT or clear the global ROOT inode */
    if (mr_fs_nsp_is_root(root)) {
        mr_dentry_uninstantiate(root);
        mr_dentry_put(root);
    } else {
        mr_dentry_del(root);
    }

    /* Put detached topology and lookup references */
    mr_dentry_put(dentry);
    mr_dentry_put(mnt);
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

static mr_err_t fs_create_node_impl(mr_dentry_t *parent, const char *name,
                                    mr_inode_type_t type,
                                    fs_node_create_op_t op, void *priv,
                                    mr_dentry_t **dentry) {
    mr_dentry_t *child, *existing;
    mr_super_block_t *sb;
    mr_inode_t *inode;
    mr_err_t ret;

    /* Init inode */
    inode = MR_NULL;

    /* Check if the parent is a directory */
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

    /* Check if the name is existing */
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
        sb = mr_super_block_get(sb);
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
        mr_super_block_put(sb);
    }
    return ret;
}

static mr_err_t fs_create_node(mr_dentry_t *parent, const char *name,
                               mr_inode_type_t type, fs_node_create_op_t op,
                               void *priv, mr_dentry_t **dentry) {
    mr_super_block_t *sb;
    mr_err_t ret;

    /* Protect the parent FS across callback and dentry commit */
    ret = fs_dentry_op_enter(parent, &sb);
    if (ret != 0) {
        return ret;
    }
    ret = fs_create_node_impl(parent, name, type, op, priv, dentry);
    fs_op_exit(sb);
    return ret;
}

static mr_err_t fs_mkdir(mr_fs_nsp_t *nsp, const char *path) {
    mr_dentry_t *parent, *dentry;
    fs_path_split_t split;
    mr_err_t ret;

    /* Split path into parent + leaf */
    ret = fs_split_path(path, &split);
    if (ret != 0) {
        return ret;
    }

    /* Look up the parent directory */
    ret = fs_lookup(nsp, split.parent, MR_TRUE, &parent);
    if (ret != 0) {
        fs_split_path_free(&split);
        return ret;
    }

    /* Create the directory node */
    ret = fs_create_node(parent, split.leaf, MR_INODE_DIR, FS_NODE_OP_CREATE,
                         MR_NULL, &dentry);
    fs_split_path_free(&split);
    mr_dentry_put(parent);
    if (ret == 0) {
        /* Drop the reference acquired by fs_create_node */
        mr_dentry_put(dentry);
    }
    return ret;
}

mr_err_t mr_fs_mkdir(const char *path) {
    mr_fs_nsp_t *nsp;

    /* Check parameter */
    MR_ASSERT((path != MR_NULL) && (path[0] != '\0'));

    /* Get current file-system namespace */
    nsp = mr_fs_nsp_current();
    if (!nsp) {
        return -MR_EINVAL;
    }

    /* Create relative to CWD */
    return fs_mkdir(nsp, path);
}

mr_err_t mr_fs_mkdirat(const mr_file_t *dir, const char *path, int flags) {
    fs_at_context_t context;
    mr_err_t ret;

    /* Check parameter */
    MR_ASSERT((path != MR_NULL) && (path[0] != '\0'));
    if (flags & (~MR_AT_IN_ROOT)) {
        return -MR_EINVAL;
    }

    /* Create relative to the pinned directory context */
    ret = fs_at_context_enter(&context, dir, path, flags);
    if (ret != 0) {
        return ret;
    }
    ret = fs_mkdir(&context.nsp, path);
    fs_at_context_release(&context);
    return ret;
}

static mr_err_t fs_mknod(mr_fs_nsp_t *nsp, const char *path,
                         mr_inode_type_t type, void *priv) {
    mr_dentry_t *parent, *dentry;
    fs_path_split_t split;
    mr_err_t ret;

    /* Split path into parent + leaf */
    ret = fs_split_path(path, &split);
    if (ret != 0) {
        return ret;
    }

    /* Look up the parent directory */
    ret = fs_lookup(nsp, split.parent, MR_TRUE, &parent);
    if (ret != 0) {
        fs_split_path_free(&split);
        return ret;
    }

    /* Create the node */
    ret = fs_create_node(parent, split.leaf, type, FS_NODE_OP_MKNOD, priv,
                         &dentry);

    /* Put resources */
    fs_split_path_free(&split);
    mr_dentry_put(parent);
    if (ret == 0) {
        mr_dentry_put(dentry);
    }
    return ret;
}

mr_err_t mr_fs_mknod(const char *path, mr_inode_type_t type, void *priv) {
    mr_fs_nsp_t *nsp;

    /* Check parameter */
    MR_ASSERT((path != MR_NULL) && (path[0] != '\0'));

    /* Check node type */
    if ((type <= MR_INODE_NONE) || (type > MR_INODE_LNK)) {
        return -MR_EINVAL;
    }

    /* Get current file-system namespace */
    nsp = mr_fs_nsp_current();
    if (!nsp) {
        return -MR_EINVAL;
    }

    /* Create relative to CWD */
    return fs_mknod(nsp, path, type, priv);
}

mr_err_t mr_fs_mknodat(const mr_file_t *dir, const char *path,
                       mr_inode_type_t type, void *priv, int flags) {
    fs_at_context_t context;
    mr_err_t ret;

    /* Check parameter */
    MR_ASSERT((path != MR_NULL) && (path[0] != '\0'));

    /* Check node type */
    if (((type <= MR_INODE_NONE) || (type > MR_INODE_LNK))
        || (flags & (~MR_AT_IN_ROOT))) {
        return -MR_EINVAL;
    }

    /* Create relative to the pinned directory context */
    ret = fs_at_context_enter(&context, dir, path, flags);
    if (ret != 0) {
        return ret;
    }
    ret = fs_mknod(&context.nsp, path, type, priv);
    fs_at_context_release(&context);
    return ret;
}

static mr_err_t fs_file_close(mr_file_t *file) {
    mr_err_t ret;

    /* Notify the FS; the handle is invalidated regardless of the result, so a
     * close error only reports a flush/finalize failure (not retryable) */
    if ((file->fops) && (file->fops->close)) {
        ret = file->fops->close(file);
    } else {
        ret = 0;
    }

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
    return 0;
_exit:
    /* Close file */
    fs_file_close(file);
    return ret;
}

static mr_err_t fs_open(mr_file_t *file, mr_fs_nsp_t *nsp, const char *path,
                        int flags) {
    mr_dentry_t *dentry, *parent;
    fs_path_split_t split;
    mr_super_block_t *sb;
    mr_inode_t *inode;
    mr_err_t ret;

    /* File must not already be open */
    if (file->inode) {
        return -MR_EBUSY;
    }

    /* Validate the access mode (only RDONLY / WRONLY / RDWR) */
    if ((flags & MR_O_ACCMODE) > MR_O_RDWR) {
        return -MR_EINVAL;
    }

    /* Try to look up the existing node */
    ret = fs_lookup(nsp, path, (flags & MR_O_NOFOLLOW) ? MR_FALSE : MR_TRUE,
                    &dentry);
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
            return -MR_ENOTDIR;
        }

        /* Split path into parent + leaf */
        ret = fs_split_path(path, &split);
        if (ret != 0) {
            return ret;
        }

        /* Look up the parent directory */
        ret = fs_lookup(nsp, split.parent, MR_TRUE, &parent);
        if (ret != 0) {
            fs_split_path_free(&split);
            return ret;
        }

        /* Create the node */
        ret = fs_create_node(parent, split.leaf, MR_INODE_REG,
                             FS_NODE_OP_CREATE, MR_NULL, &dentry);
        fs_split_path_free(&split);
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
    ret = fs_dentry_op_enter(dentry, &sb);
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
    mr_fs_nsp_t *nsp;

    /* Check parameter */
    MR_ASSERT((path != MR_NULL) && (path[0] != '\0'));
    MR_ASSERT(file != MR_NULL);

    /* Get current file-system namespace */
    nsp = mr_fs_nsp_current();
    if (!nsp) {
        return -MR_EINVAL;
    }

    /* Open relative to CWD */
    if (flags & MR_O_IN_ROOT) {
        return -MR_EINVAL;
    }
    return fs_open(file, nsp, path, flags);
}

mr_err_t mr_fs_openat(mr_file_t *file, const mr_file_t *dir, const char *path,
                      int flags) {
    fs_at_context_t context;
    mr_err_t ret;
    int at_flags;

    /* Check parameter */
    MR_ASSERT((path != MR_NULL) && (path[0] != '\0'));
    MR_ASSERT(file != MR_NULL);

    /* Open relative to the pinned directory */
    at_flags = (flags & MR_O_IN_ROOT) ? MR_AT_IN_ROOT : 0;
    ret = fs_at_context_enter(&context, dir, path, at_flags);
    if (ret != 0) {
        return ret;
    }
    ret = fs_open(file, &context.nsp, path, flags & (~MR_O_IN_ROOT));
    fs_at_context_release(&context);
    return ret;
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

mr_err_t mr_fs_opendir(mr_file_t *file, const char *path) {
    /* Open the directory read-only; mr_fs_open enforces that the node is a
     * directory(-MR_ENOTDIR) and read-only(-MR_EISDIR) */
    return mr_fs_open(file, path, MR_O_DIRECTORY | MR_O_RDONLY);
}

static mr_err_t fs_dentry_readdir(mr_file_t *file, mr_dirent_t *dirent) {
    mr_dentry_t *child;
    mr_size_t index;

    /* Get child dentry */
    index = (mr_size_t)file->pos;
    child = mr_dentry_child_get(file->dentry, index);
    if (!child) {
        return 0;
    }

    /* Read directory entry */
    mr_strncpy(dirent->name, MR_DENTRY_NAME(child), MR_CFG_FS_PATH_LEN - 1);
    dirent->name[MR_CFG_FS_PATH_LEN - 1] = '\0';
    dirent->type = child->inode ? child->inode->type : MR_INODE_NONE;
    mr_dentry_put(child);

    /* Update position */
    file->pos = (mr_int64_t)(index + 1);
    return 1;
}

mr_err_t mr_fs_readdir(mr_file_t *file, mr_dirent_t *dirent) {
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

    /* Read the next entry */
    if (file->fops && file->fops->readdir) {
        return file->fops->readdir(file, dirent);
    }

    /* Use the default implementation */
    return fs_dentry_readdir(file, dirent);
}

mr_err_t mr_fs_closedir(mr_file_t *file) {
    /* Close the directory handle */
    return mr_fs_close(file);
}

static mr_err_t fs_stat_inode(mr_inode_t *inode, mr_stat_t *stat) {
    /* Fill generic inode status */
    stat->type = inode->type;
    stat->mode = inode->mode;
    stat->nlink = inode->nlink;
    stat->size = inode->size;

    /* Let the FS override dynamic fields if it provides a hook */
    if ((inode->ops) && (inode->ops->stat)) {
        return inode->ops->stat(inode, stat);
    }
    return 0;
}

mr_err_t mr_fs_stat(const mr_file_t *file, mr_stat_t *stat) {
    /* Check parameter */
    MR_ASSERT(file != MR_NULL);
    MR_ASSERT(stat != MR_NULL);

    /* File must be open */
    if (!file->inode) {
        return -MR_EBADF;
    }

    /* Fill inode status */
    return fs_stat_inode(file->inode, stat);
}

static mr_err_t fs_stat_by_path(mr_fs_nsp_t *nsp, const char *path,
                                mr_stat_t *stat, mr_bool_t follow_final) {
    mr_super_block_t *sb;
    mr_dentry_t *dentry;
    mr_err_t ret;

    /* Look up the path */
    ret = fs_lookup(nsp, path, follow_final, &dentry);
    if (ret != 0) {
        return ret;
    }

    /* Protect the resolved FS across stat callback */
    ret = fs_dentry_op_enter(dentry, &sb);
    if (ret == 0) {
        ret = fs_stat_inode(dentry->inode, stat);
        fs_op_exit(sb);
    }
    mr_dentry_put(dentry);
    return ret;
}

mr_err_t mr_fs_stat_by_path(const char *path, mr_stat_t *stat) {
    mr_fs_nsp_t *nsp;

    /* Check parameter */
    MR_ASSERT((path != MR_NULL) && (path[0] != '\0'));
    MR_ASSERT(stat != MR_NULL);

    /* Get current file-system namespace */
    nsp = mr_fs_nsp_current();
    if (!nsp) {
        return -MR_EINVAL;
    }

    /* Follow the final symbolic link */
    return fs_stat_by_path(nsp, path, stat, MR_TRUE);
}

mr_err_t mr_fs_statat(const mr_file_t *dir, const char *path, mr_stat_t *stat,
                      int flags) {
    fs_at_context_t context;
    mr_bool_t follow_final;
    mr_err_t ret;

    /* Check parameter */
    MR_ASSERT((path != MR_NULL) && (path[0] != '\0'));
    MR_ASSERT(stat != MR_NULL);

    /* Check flags */
    if (flags & (~(MR_AT_IN_ROOT | MR_AT_SYMLINK_NOFOLLOW))) {
        return -MR_EINVAL;
    }

    /* Get status relative to the pinned directory context */
    ret = fs_at_context_enter(&context, dir, path, flags);
    if (ret != 0) {
        return ret;
    }
    follow_final = (flags & MR_AT_SYMLINK_NOFOLLOW) ? MR_FALSE : MR_TRUE;
    ret = fs_stat_by_path(&context.nsp, path, stat, follow_final);
    fs_at_context_release(&context);
    return ret;
}

static mr_bool_t fs_dentry_is_mount(mr_dentry_t *dentry) {
    mr_super_block_t *sb;

    /* Find either side of a mount boundary */
    sb = mr_super_block_find_by_dentry(dentry);
    if (!sb) {
        return MR_FALSE;
    }
    mr_super_block_put(sb);
    return MR_TRUE;
}

static mr_err_t fs_link(const mr_file_t *file, const char *new_path) {
    mr_dentry_t *parent, *child, *existing;
    mr_super_block_t *sb, *target_guard;
    fs_path_split_t split;
    mr_inode_t *inode;
    mr_fs_nsp_t *nsp;
    mr_err_t ret;
    int mask;

    /* Check source */
    inode = file->inode;
    if (!file->dentry) {
        return -MR_ENOENT;
    }
    if (inode->type == MR_INODE_DIR) {
        return -MR_EPERM;
    }

    /* Split and resolve destination parent */
    ret = fs_split_path(new_path, &split);
    if (ret != 0) {
        return ret;
    }
    nsp = mr_fs_nsp_current();
    if (!nsp) {
        ret = -MR_EINVAL;
        goto _exit_free;
    }
    ret = fs_lookup(nsp, split.parent, MR_TRUE, &parent);
    if (ret != 0) {
        goto _exit_free;
    }
    target_guard = MR_NULL;
    ret = fs_dentry_op_enter(parent, &target_guard);
    if (ret != 0) {
        goto _exit_parent;
    }
    if ((!parent->inode) || (parent->inode->type != MR_INODE_DIR)) {
        ret = -MR_ENOTDIR;
        goto _exit_parent;
    }
    if (parent->inode->sb != inode->sb) {
        ret = -MR_EXDEV;
        goto _exit_parent;
    }
    /* Reject an existing destination */
    ret = mr_dentry_lookup(parent, parent, split.leaf, &existing);
    if (ret == 0) {
        mr_dentry_put(existing);
        ret = -MR_EEXIST;
        goto _exit_parent;
    } else if (ret != (-MR_ENOENT)) {
        goto _exit_parent;
    }

    /* Reserve destination dentry */
    sb = parent->inode->sb;
    child = mr_dentry_create(sb ? sb->dops : MR_NULL, "%s", split.leaf);
    if (!child) {
        ret = -MR_ENOMEM;
        goto _exit_parent;
    }
    ret = mr_dentry_add(child, parent);
    if (ret != 0) {
        mr_dentry_del(child);
        goto _exit_parent;
    }

    /* Lock */
    mask = mr_fs_nsp_lock();

    /* Reserve an inode link while the file-system creates its directory
     * entry */
    if ((file->dentry->inode != inode) || (inode->nlink == 0)) {
        ret = -MR_ENOENT;
    } else {
        inode->nlink++;
        ret = 0;
    }

    /* Unlock */
    mr_fs_nsp_unlock(mask);

    /* Check for link creation failure */
    if (ret != 0) {
        mr_dentry_del(child);
        goto _exit_parent;
    }

    /* Create backing link when the FS provides a callback */
    if (parent->inode->ops && parent->inode->ops->link) {
        ret = parent->inode->ops->link(parent->inode, split.leaf, inode);
    } else {
        ret = 0;
    }
    if (ret != 0) {
        /* Lock */
        mask = mr_fs_nsp_lock();

        /* Decrement link count */
        inode->nlink--;

        /* Unlock */
        mr_fs_nsp_unlock(mask);
        mr_dentry_del(child);
        goto _exit_parent;
    }

    /* Bind the shared inode */
    ret = mr_dentry_instantiate(child, inode);
    if (ret != 0) {
        /* Lock */
        mask = mr_fs_nsp_lock();

        /* Decrement link count */
        inode->nlink--;

        /* Unlock */
        mr_fs_nsp_unlock(mask);
        mr_dentry_del(child);
    }
_exit_parent:
    if (target_guard) {
        fs_op_exit(target_guard);
    }
    mr_dentry_put(parent);
_exit_free:
    fs_split_path_free(&split);
    return ret;
}

mr_err_t mr_fs_link(const mr_file_t *file, const char *new_path) {
    mr_super_block_t *sb;
    mr_err_t ret;

    /* Check parameter */
    MR_ASSERT(file != MR_NULL);
    MR_ASSERT((new_path != MR_NULL) && (new_path[0] != '\0'));

    /* File must be open */
    if (!file->inode) {
        return -MR_EBADF;
    }

    /* Protect source and destination FS across link commit */
    ret = fs_inode_op_enter(file->inode, &sb);
    if (ret != 0) {
        return ret;
    }
    ret = fs_link(file, new_path);
    fs_op_exit(sb);
    return ret;
}

static mr_err_t fs_symlink_create_default(const char *target,
                                          mr_inode_t **out) {
    char *stored_target;
    mr_inode_t *inode;
    mr_size_t len;

    /* Copy symbolic-link target */
    len = mr_strlen(target);
    stored_target = mr_malloc(len);
    if (!stored_target) {
        *out = MR_NULL;
        return -MR_ENOMEM;
    }
    mr_memcpy(stored_target, target, len);

    /* Create symbolic-link inode */
    inode = mr_inode_create(MR_INODE_LNK, &__fs_symlink_ops, MR_NULL,
                            stored_target);
    if (!inode) {
        mr_free(stored_target);
        *out = MR_NULL;
        return -MR_ENOMEM;
    }
    inode->size = (mr_int64_t)len;
    *out = inode;
    return 0;
}

mr_err_t mr_fs_symlink(const char *target, const char *link_path) {
    mr_dentry_t *parent, *child, *existing;
    mr_super_block_t *sb, *guard;
    fs_path_split_t split;
    mr_inode_t *inode;
    mr_fs_nsp_t *nsp;
    mr_err_t ret;

    /* Check parameter */
    MR_ASSERT((target != MR_NULL) && (target[0] != '\0'));
    MR_ASSERT((link_path != MR_NULL) && (link_path[0] != '\0'));

    /* Split and resolve parent */
    ret = fs_split_path(link_path, &split);
    if (ret != 0) {
        return ret;
    }
    nsp = mr_fs_nsp_current();
    if (!nsp) {
        ret = -MR_EINVAL;
        goto _exit_free;
    }
    ret = fs_lookup(nsp, split.parent, MR_TRUE, &parent);
    if (ret != 0) {
        goto _exit_free;
    }
    guard = MR_NULL;
    ret = fs_dentry_op_enter(parent, &guard);
    if (ret != 0) {
        goto _exit_parent;
    }
    if ((!parent->inode) || (parent->inode->type != MR_INODE_DIR)) {
        ret = -MR_ENOTDIR;
        goto _exit_parent;
    }

    /* Reject an existing destination */
    ret = mr_dentry_lookup(parent, parent, split.leaf, &existing);
    if (ret == 0) {
        mr_dentry_put(existing);
        ret = -MR_EEXIST;
        goto _exit_parent;
    } else if (ret != (-MR_ENOENT)) {
        goto _exit_parent;
    }

    /* Reserve destination and super-block */
    sb = parent->inode->sb;
    child = mr_dentry_create(sb ? sb->dops : MR_NULL, "%s", split.leaf);
    if (!child) {
        ret = -MR_ENOMEM;
        goto _exit_parent;
    }
    if (sb) {
        sb = mr_super_block_get(sb);
        if (!sb) {
            mr_dentry_del(child);
            ret = -MR_ENOENT;
            goto _exit_parent;
        }
    }
    ret = mr_dentry_add(child, parent);
    if (ret != 0) {
        goto _exit_child;
    }

    /* Create backing or built-in symbolic link */
    inode = MR_NULL;
    if (parent->inode->ops && parent->inode->ops->symlink) {
        ret = parent->inode->ops->symlink(parent->inode, split.leaf, target,
                                          &inode);
    } else {
        ret = fs_symlink_create_default(target, &inode);
    }
    if (ret != 0) {
        goto _exit_child;
    }
    if ((!inode) || (inode->type != MR_INODE_LNK) || inode->sb) {
        ret = -MR_EINVAL;
        goto _exit_inode;
    }
    inode->sb = sb;
    sb = MR_NULL;

    /* Bind inode */
    ret = mr_dentry_instantiate(child, inode);
_exit_inode:
    if (inode) {
        mr_inode_put(inode);
    }
    if (ret == 0) {
        goto _exit_parent;
    }
_exit_child:
    mr_dentry_del(child);
    if (sb) {
        mr_super_block_put(sb);
    }
_exit_parent:
    if (guard) {
        fs_op_exit(guard);
    }
    mr_dentry_put(parent);
_exit_free:
    fs_split_path_free(&split);
    return ret;
}

mr_ssize_t mr_fs_readlink(const mr_file_t *file, char *buf, mr_size_t size) {
    mr_inode_t *inode;

    /* Check parameter */
    MR_ASSERT(file != MR_NULL);
    MR_ASSERT((buf != MR_NULL) || (size == 0));

    /* Check symbolic-link handle */
    inode = file->inode;
    if (!inode) {
        return -MR_EBADF;
    }
    if (inode->type != MR_INODE_LNK) {
        return -MR_EINVAL;
    }
    if ((!inode->ops) || (!inode->ops->readlink)) {
        return -MR_ENOSYS;
    }
    if (size == 0) {
        return 0;
    }

    /* Read target */
    return inode->ops->readlink(inode, buf, size);
}

static mr_err_t fs_rename_dentry(mr_dentry_t *old_dentry, mr_inode_t *inode,
                                 mr_fs_nsp_t *nsp, const char *new_path) {
    mr_dentry_t *old_parent, *new_parent;
    mr_super_block_t *target_guard;
    fs_path_split_t split;
    mr_err_t ret;

    /* Dentry must still refer to the inode */
    if ((!old_dentry) || (old_dentry->inode != inode)) {
        return -MR_ENOENT;
    }

    /* A mount-point directory can not be renamed */
    if (fs_dentry_is_mount(old_dentry)) {
        return -MR_EBUSY;
    }

    /* Source must have a directory parent */
    old_parent = old_dentry->upper;
    if (!old_parent) {
        return -MR_ENOENT;
    }

    /* Source parent must be a directory */
    if ((!old_parent->inode) || (old_parent->inode->type != MR_INODE_DIR)) {
        return -MR_ENOTDIR;
    }

    /* Split the destination into parent + leaf */
    ret = fs_split_path(new_path, &split);
    if (ret != 0) {
        return ret;
    }

    /* Look up the destination parent directory */
    ret = fs_lookup(nsp, split.parent, MR_TRUE, &new_parent);
    if (ret != 0) {
        goto _exit_free;
    }
    target_guard = MR_NULL;
    ret = fs_dentry_op_enter(new_parent, &target_guard);
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

    /* Renaming across FS is not supported */
    if (old_parent->inode->sb && new_parent->inode->sb
        && (old_parent->inode->sb != new_parent->inode->sb)) {
        ret = -MR_EXDEV;
        goto _exit_put;
    }

    /* Rename backing node and dentry */
    ret = mr_dentry_rename(old_dentry, new_parent, split.leaf);
_exit_put:
    if (target_guard) {
        fs_op_exit(target_guard);
    }
    mr_dentry_put(new_parent);
_exit_free:
    fs_split_path_free(&split);
    return ret;
}

mr_err_t mr_fs_rename(mr_file_t *file, const char *new_path) {
    mr_super_block_t *sb;
    mr_fs_nsp_t *nsp;
    mr_err_t ret;

    /* Check parameter */
    MR_ASSERT(file != MR_NULL);
    MR_ASSERT((new_path != MR_NULL) && (new_path[0] != '\0'));

    /* File must be open */
    if (!file->inode) {
        return -MR_EBADF;
    }

    /* Get current file-system namespace */
    nsp = mr_fs_nsp_current();
    if (!nsp) {
        return -MR_EINVAL;
    }

    /* Protect source and destination FS across rename commit */
    ret = fs_inode_op_enter(file->inode, &sb);
    if (ret != 0) {
        return ret;
    }
    ret = fs_rename_dentry(file->dentry, file->inode, nsp, new_path);
    fs_op_exit(sb);
    return ret;
}

static mr_err_t fs_rename_by_path(mr_fs_nsp_t *old_nsp, const char *old_path,
                                  mr_fs_nsp_t *new_nsp, const char *new_path,
                                  mr_bool_t protect_root) {
    mr_super_block_t *sb;
    mr_dentry_t *dentry;
    mr_err_t ret;

    /* Look up and rename the path itself */
    ret = fs_lookup(old_nsp, old_path, MR_FALSE, &dentry);
    if (ret != 0) {
        return ret;
    }

    /* A lookup root can not be renamed through its confined context */
    if (protect_root && (dentry == old_nsp->root)) {
        mr_dentry_put(dentry);
        return -MR_EBUSY;
    }

    /* Protect source and destination FS across rename commit */
    ret = fs_dentry_op_enter(dentry, &sb);
    if (ret == 0) {
        ret = fs_rename_dentry(dentry, dentry->inode, new_nsp, new_path);
        fs_op_exit(sb);
    }
    mr_dentry_put(dentry);
    return ret;
}

mr_err_t mr_fs_rename_by_path(const char *old_path, const char *new_path) {
    mr_fs_nsp_t *nsp;

    /* Check parameter */
    MR_ASSERT((old_path != MR_NULL) && (old_path[0] != '\0'));
    MR_ASSERT((new_path != MR_NULL) && (new_path[0] != '\0'));

    /* Get current file-system namespace */
    nsp = mr_fs_nsp_current();
    if (!nsp) {
        return -MR_EINVAL;
    }

    /* Rename relative to CWD */
    return fs_rename_by_path(nsp, old_path, nsp, new_path, MR_FALSE);
}

mr_err_t mr_fs_renameat(const mr_file_t *old_dir, const char *old_path,
                        const mr_file_t *new_dir, const char *new_path,
                        int flags) {
    fs_at_context_t old_context, new_context;
    mr_bool_t protect_root;
    mr_err_t ret;

    /* Check parameter */
    MR_ASSERT((old_path != MR_NULL) && (old_path[0] != '\0'));
    MR_ASSERT((new_path != MR_NULL) && (new_path[0] != '\0'));
    if (flags & (~MR_AT_IN_ROOT)) {
        return -MR_EINVAL;
    }

    /* Pin source and destination directory contexts */
    ret = fs_at_context_enter(&old_context, old_dir, old_path, flags);
    if (ret != 0) {
        return ret;
    }
    ret = fs_at_context_enter(&new_context, new_dir, new_path, flags);
    if (ret != 0) {
        fs_at_context_release(&old_context);
        return ret;
    }

    /* Rename between the pinned contexts */
    protect_root = (flags & MR_AT_IN_ROOT) ? MR_TRUE : MR_FALSE;
    ret = fs_rename_by_path(&old_context.nsp, old_path, &new_context.nsp,
                            new_path, protect_root);
    fs_at_context_release(&new_context);
    fs_at_context_release(&old_context);
    return ret;
}

static mr_err_t fs_unlink_dentry(mr_dentry_t *dentry, mr_inode_t *inode) {
    mr_dentry_t *parent;

    /* Dentry must still refer to the inode */
    if ((!dentry) || (dentry->inode != inode)) {
        return -MR_ENOENT;
    }

    /* A mount-point directory can not be removed */
    if (fs_dentry_is_mount(dentry)) {
        return -MR_EBUSY;
    }

    /* Root has no parent and can not be unlinked */
    parent = dentry->upper;
    if (!parent) {
        return -MR_ENOENT;
    }

    /* Check the parent is a directory */
    if ((!parent->inode) || (parent->inode->type != MR_INODE_DIR)) {
        return -MR_ENOTDIR;
    }

    /* A non-empty directory can not be removed */
    if ((inode->type == MR_INODE_DIR) && (!mr_dentry_is_empty(dentry))) {
        return -MR_ENOTEMPTY;
    }

    /* Remove backing entry and commit dentry unlink */
    return mr_dentry_unlink(dentry);
}

mr_err_t mr_fs_unlink(mr_file_t *file) {
    mr_super_block_t *sb;
    mr_err_t ret;

    /* Check parameter */
    MR_ASSERT(file != MR_NULL);

    /* File must be open */
    if (!file->inode) {
        return -MR_EBADF;
    }

    /* Protect the backing FS across unlink commit */
    ret = fs_inode_op_enter(file->inode, &sb);
    if (ret != 0) {
        return ret;
    }
    ret = fs_unlink_dentry(file->dentry, file->inode);
    fs_op_exit(sb);
    return ret;
}

mr_err_t mr_fs_unlink_recursive(mr_file_t *file) {
    /* Check parameter */
    MR_ASSERT(file != MR_NULL);

    /* File must be open */
    if (!file->inode) {
        return -MR_EBADF;
    }

    /* Non-directories are recursive leaves */
    if (file->inode->type != MR_INODE_DIR) {
        return mr_fs_unlink(file);
    }

    /* Recursively remove the directory through its current dentry */
    return mr_fs_unlinkat_recursive(file, ".", 0);
}

static mr_err_t fs_unlink_by_path(mr_fs_nsp_t *nsp, const char *path,
                                  mr_bool_t protect_root) {
    mr_super_block_t *sb;
    mr_dentry_t *dentry;
    mr_err_t ret;

    /* Look up and unlink the path itself */
    ret = fs_lookup(nsp, path, MR_FALSE, &dentry);
    if (ret != 0) {
        return ret;
    }

    /* A lookup root can not be removed through its confined context */
    if (protect_root && (dentry == nsp->root)) {
        mr_dentry_put(dentry);
        return -MR_EBUSY;
    }

    /* Protect the backing FS across unlink commit */
    ret = fs_dentry_op_enter(dentry, &sb);
    if (ret == 0) {
        ret = fs_unlink_dentry(dentry, dentry->inode);
        fs_op_exit(sb);
    }
    mr_dentry_put(dentry);
    return ret;
}

mr_err_t mr_fs_unlink_by_path(const char *path) {
    mr_fs_nsp_t *nsp;

    /* Check parameter */
    MR_ASSERT((path != MR_NULL) && (path[0] != '\0'));

    /* Get current file-system namespace */
    nsp = mr_fs_nsp_current();
    if (!nsp) {
        return -MR_EINVAL;
    }

    /* Unlink relative to CWD */
    return fs_unlink_by_path(nsp, path, MR_FALSE);
}

static mr_err_t fs_unlink_dentry_guarded(mr_dentry_t *dentry) {
    mr_super_block_t *sb;
    mr_err_t ret;

    /* Protect the backing FS across unlink commit */
    ret = fs_dentry_op_enter(dentry, &sb);
    if (ret != 0) {
        return ret;
    }
    ret = fs_unlink_dentry(dentry, dentry->inode);
    fs_op_exit(sb);
    return ret;
}

static mr_err_t fs_unlink_directory_first(mr_dentry_t *dentry,
                                          mr_dentry_t **child) {
    const int flags = MR_O_RDONLY | MR_O_DIRECTORY | MR_O_NOFOLLOW;
    mr_file_t file = MR_FILE_INIT(file);
    mr_super_block_t *sb;
    mr_size_t index, len;
    mr_dirent_t dirent;
    mr_err_t close_ret;
    mr_err_t ret;

    /* Prefer cached children, including namespace-only nodes */
    *child = mr_dentry_child_get(dentry, 0);
    if (*child) {
        return 1;
    }

    /* Open the resolved directory directly */
    ret = fs_dentry_op_enter(dentry, &sb);
    if (ret != 0) {
        return ret;
    }
    if ((!dentry->inode) || (dentry->inode->type != MR_INODE_DIR)) {
        ret = dentry->inode ? -MR_ENOTDIR : -MR_ENOENT;
    } else {
        ret = fs_file_open(&file, dentry, dentry->inode, flags);
    }
    fs_op_exit(sb);
    if (ret != 0) {
        return ret;
    }

    /* Read the first real backing directory entry */
    while (1) {
        ret = mr_fs_readdir(&file, &dirent);
        if (ret <= 0) {
            break;
        }

        /* Validate the file-system-provided component */
        len = mr_strnlen(dirent.name, sizeof(dirent.name));
        if ((len == 0) || (len >= sizeof(dirent.name))) {
            ret = -MR_EIO;
            break;
        }
        if ((mr_strcmp(dirent.name, "..") == 0)
            || (mr_strcmp(dirent.name, ".") == 0)) {
            continue;
        }
        for (index = 0; index < len; index++) {
            if (dirent.name[index] == '/') {
                ret = -MR_EIO;
                break;
            }
        }
        if (ret < 0) {
            break;
        }
        ret = 1;
        break;
    }

    /* Close the directory and preserve the first failure */
    close_ret = fs_file_close(&file);
    if ((ret >= 0) && (close_ret != 0)) {
        ret = close_ret;
    }
    if (ret <= 0) {
        return ret;
    }

    /* Resolve the backing entry relative to its parent */
    ret = mr_dentry_lookup(dentry, dentry, dirent.name, child);
    return (ret == 0) ? 1 : ret;
}

static mr_err_t fs_unlink_recursive(mr_fs_nsp_t *nsp, const char *path) {
    mr_super_block_t *sb;
    mr_inode_type_t type;
    mr_dentry_t *root, *current, *child, *parent;
    mr_err_t ret;
    int mask;

    /* Resolve and pin the subtree root */
    ret = fs_lookup(nsp, path, MR_FALSE, &root);
    if (ret != 0) {
        return ret;
    }
    current = mr_dentry_get(root);
    if (!current) {
        mr_dentry_put(root);
        return -MR_ENOENT;
    }

    /* Walk to each leaf and delete the subtree in post-order */
    while (current) {
        ret = fs_dentry_op_enter(current, &sb);
        if (ret != 0) {
            break;
        }

        /* Never cross or remove a lookup root or mount boundary */
        if ((current == nsp->root) || fs_dentry_is_mount(current)) {
            ret = -MR_EBUSY;
            fs_op_exit(sb);
            break;
        }

        /* Snapshot the node type while its FS is active */
        if (!current->inode) {
            ret = -MR_ENOENT;
            fs_op_exit(sb);
            break;
        }
        type = current->inode->type;
        fs_op_exit(sb);

        /* Descend into the first remaining child */
        if (type == MR_INODE_DIR) {
            ret = fs_unlink_directory_first(current, &child);
            if (ret < 0) {
                break;
            }
            if (ret > 0) {
                mr_dentry_put(current);
                current = child;
                continue;
            }
        }

        /* Pin the parent before unlink drops the namespace reference */
        parent = MR_NULL;
        if (current != root) {
            mask = mr_fs_nsp_lock();
            if (current->upper) {
                parent = mr_dentry_get(current->upper);
            }
            mr_fs_nsp_unlock(mask);
            if (!parent) {
                ret = -MR_ENOENT;
                break;
            }
        }

        /* Remove the leaf and continue from its parent */
        ret = fs_unlink_dentry_guarded(current);
        if (ret != 0) {
            if (parent) {
                mr_dentry_put(parent);
            }
            break;
        }
        mr_dentry_put(current);
        current = parent;
    }

    /* Release traversal references */
    if (current) {
        mr_dentry_put(current);
    }
    mr_dentry_put(root);
    return ret;
}

mr_err_t mr_fs_unlink_recursive_by_path(const char *path) {
    mr_fs_nsp_t *nsp;

    /* Check parameter */
    MR_ASSERT((path != MR_NULL) && (path[0] != '\0'));

    /* Get current file-system namespace */
    nsp = mr_fs_nsp_current();
    if (!nsp) {
        return -MR_EINVAL;
    }

    /* Recursively unlink relative to CWD */
    return fs_unlink_recursive(nsp, path);
}

mr_err_t mr_fs_unlinkat(const mr_file_t *dir, const char *path, int flags) {
    fs_at_context_t context;
    mr_bool_t protect_root;
    mr_err_t ret;

    /* Check parameter */
    MR_ASSERT((path != MR_NULL) && (path[0] != '\0'));
    if (flags & (~MR_AT_IN_ROOT)) {
        return -MR_EINVAL;
    }

    /* Unlink relative to the pinned directory context */
    ret = fs_at_context_enter(&context, dir, path, flags);
    if (ret != 0) {
        return ret;
    }
    protect_root = (flags & MR_AT_IN_ROOT) ? MR_TRUE : MR_FALSE;
    ret = fs_unlink_by_path(&context.nsp, path, protect_root);
    fs_at_context_release(&context);
    return ret;
}

mr_err_t mr_fs_unlinkat_recursive(const mr_file_t *dir, const char *path,
                                  int flags) {
    fs_at_context_t context;
    mr_err_t ret;

    /* Check parameter */
    MR_ASSERT((path != MR_NULL) && (path[0] != '\0'));
    if (flags & (~MR_AT_IN_ROOT)) {
        return -MR_EINVAL;
    }

    /* Recursively unlink relative to the pinned directory context */
    ret = fs_at_context_enter(&context, dir, path, flags);
    if (ret != 0) {
        return ret;
    }
    ret = fs_unlink_recursive(&context.nsp, path);
    fs_at_context_release(&context);
    return ret;
}

mr_ssize_t mr_fs_read(mr_file_t *file, void *buf, mr_size_t size) {
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

    /* Read from the file */
    return file->fops->read(file, buf, size);
}

mr_ssize_t mr_fs_write(mr_file_t *file, const void *buf, mr_size_t size) {
    mr_int64_t previous_pos;
    mr_ssize_t ret;

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

    /* Select the latest EOF for every append write */
    previous_pos = file->pos;
    if ((file->flags & MR_O_APPEND) != 0) {
        file->pos = file->inode->size;
    }

    /* Write to the file */
    ret = file->fops->write(file, buf, size);
    if (((file->flags & MR_O_APPEND) != 0) && (ret <= 0)) {
        file->pos = previous_pos;
    }
    return ret;
}

mr_int64_t mr_fs_seek(mr_file_t *file, mr_int64_t offset, int whence) {
    mr_int64_t position;
    mr_int64_t base;

    /* Check parameter */
    MR_ASSERT(file != MR_NULL);

    /* File must be open */
    if (!file->inode) {
        return -MR_EBADF;
    }

    /* Seek the file */
    if (file->fops && file->fops->llseek) {
        return file->fops->llseek(file, offset, whence);
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
            return -MR_EINVAL;
        }
    }

    /* Check range before adding signed values */
    if (((offset < 0) && (base < ((-MR_INT64_MAX - 1) - offset)))
        || ((offset > 0) && (base > (MR_INT64_MAX - offset)))) {
        return -MR_ERANGE;
    }

    /* Set position(clamped to >= 0) */
    position = base + offset;
    if (position < 0) {
        position = 0;
    }
    file->pos = position;
    return position;
}

mr_int64_t mr_fs_tell(mr_file_t *file) {
    /* Check parameter */
    MR_ASSERT(file != MR_NULL);

    /* File must be open */
    if (!file->inode) {
        return -MR_EBADF;
    }

    /* Return the current position */
    return file->pos;
}

mr_err_t mr_fs_truncate(mr_file_t *file, mr_int64_t length) {
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

    /* Truncate the file */
    return file->inode->ops->truncate(file->inode, file, length);
}

mr_err_t mr_fs_sync(mr_file_t *file) {
    /* Check parameter */
    MR_ASSERT(file != MR_NULL);

    /* File must be open */
    if (!file->inode) {
        return -MR_EBADF;
    }

    /* Check the FS supports sync */
    if ((!file->fops) || (!file->fops->sync)) {
        return -MR_ENOSYS;
    }

    /* Sync the file */
    return file->fops->sync(file);
}

mr_err_t mr_fs_ioctl(mr_file_t *file, int cmd, void *arg) {
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

    /* Control the file */
    return file->fops->ioctl(file, cmd, arg);
}

mr_err_t mr_fs_chroot(const char *path) {
    mr_dentry_t *dentry, *cwd, *old_root, *old_cwd;
    mr_super_block_t *sb;
    mr_inode_t *inode;
    mr_fs_nsp_t *nsp;
    mr_err_t ret;
    int mask;

    /* Check parameter */
    MR_ASSERT((path != MR_NULL) && (path[0] != '\0'));

    /* Get current file-system namespace */
    nsp = mr_fs_nsp_current();
    if (!nsp) {
        return -MR_EINVAL;
    }

    /* Look up the new ROOT */
    ret = fs_lookup(nsp, path, MR_TRUE, &dentry);
    if (ret != 0) {
        return ret;
    }

    /* Protect the new ROOT while updating namespace context */
    ret = fs_dentry_op_enter(dentry, &sb);
    if (ret != 0) {
        mr_dentry_put(dentry);
        return ret;
    }

    /* Lock */
    mask = mr_fs_nsp_lock();

    /* New ROOT must still be a directory */
    inode = dentry->inode;
    if ((!inode) || (inode->type != MR_INODE_DIR)) {
        /* Unlock */
        mr_fs_nsp_unlock(mask);
        fs_op_exit(sb);
        mr_dentry_put(dentry);
        return -MR_ENOTDIR;
    }

    /* Keep CWD only when it is contained by the new ROOT */
    for (cwd = nsp->cwd; cwd; cwd = cwd->upper) {
        if (cwd == dentry) {
            cwd = nsp->cwd;
            break;
        }
    }

    /* Reset an external CWD to the new ROOT */
    if (!cwd) {
        cwd = mr_dentry_get(dentry);
        if (!cwd) {
            /* Unlock */
            mr_fs_nsp_unlock(mask);
            fs_op_exit(sb);
            mr_dentry_put(dentry);
            return -MR_EINVAL;
        }
    }

    /* Swap the context ROOT */
    old_root = nsp->root;
    old_cwd = nsp->cwd;
    nsp->root = dentry;
    nsp->cwd = cwd;

    /* Unlock */
    mr_fs_nsp_unlock(mask);
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
    mr_super_block_t *sb;
    mr_inode_t *inode;
    mr_fs_nsp_t *nsp;
    mr_err_t ret;
    int mask;

    /* Check parameter */
    MR_ASSERT((path != MR_NULL) && (path[0] != '\0'));

    /* Get current file-system namespace */
    nsp = mr_fs_nsp_current();
    if (!nsp) {
        return -MR_EINVAL;
    }

    /* Look up the new working directory */
    ret = fs_lookup(nsp, path, MR_TRUE, &dentry);
    if (ret != 0) {
        return ret;
    }

    /* Protect the new CWD while updating namespace context */
    ret = fs_dentry_op_enter(dentry, &sb);
    if (ret != 0) {
        mr_dentry_put(dentry);
        return ret;
    }

    /* Lock */
    mask = mr_fs_nsp_lock();

    /* New working directory must still be a directory */
    inode = dentry->inode;
    if ((!inode) || (inode->type != MR_INODE_DIR)) {
        /* Unlock */
        mr_fs_nsp_unlock(mask);
        fs_op_exit(sb);
        mr_dentry_put(dentry);
        return -MR_ENOTDIR;
    }

    /* Swap the context CWD */
    old_cwd = nsp->cwd;
    nsp->cwd = dentry;

    /* Unlock */
    mr_fs_nsp_unlock(mask);
    fs_op_exit(sb);

    /* Put old CWD */
    mr_dentry_put(old_cwd);
    return 0;
}

mr_ssize_t mr_fs_getcwd(char *buf, mr_size_t size) {
    mr_size_t len, pos, name_len;
    mr_dentry_t *dentry;
    mr_fs_nsp_t *nsp;
    mr_ssize_t ret;
    int mask;

    /* Check parameter */
    MR_ASSERT((buf != MR_NULL) && (size > 0));

    /* Get current file-system namespace */
    nsp = mr_fs_nsp_current();
    if (!nsp) {
        return -MR_EINVAL;
    }

    /* Lock */
    mask = mr_fs_nsp_lock();

    /* Check namespace context */
    if ((!nsp->root) || (!nsp->cwd)) {
        ret = -MR_EINVAL;
        goto _exit;
    }

    /* CWD is the ROOT */
    dentry = nsp->cwd;
    if (dentry == nsp->root) {
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
    while (dentry != nsp->root) {
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
    dentry = nsp->cwd;
    pos = len;
    buf[pos] = '\0';
    while (dentry != nsp->root) {
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
    mr_fs_nsp_unlock(mask);
    return ret;
}

void mr_fs_file_init(void) {
#if !defined(MR_USE_FS_MULTIUSER)
    /* Init single user FS namespace */
    mr_fs_nsp_init(mr_fs_nsp_current());
#endif /* !defined(MR_USE_FS_MULTIUSER) */
}
#endif /* defined(MR_USE_FS) */
