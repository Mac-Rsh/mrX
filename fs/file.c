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

/* FS path split type */
typedef struct fs_path_split {
    char ipath[MR_CFG_FS_DENTRY_NAME_LEN * 2];
    const char *leaf;
    char *parent;
} fs_path_split_t;

/* Global FS root dentry("/") */
static mr_dentry_t __fs_root = MR_DENTRY_ROOT_INIT(&__fs_root);
#if !defined(MR_USE_FS_MULTIUSER)
static mr_fs_struct_t __fs_struct;
#endif /* !defined(MR_USE_FS_MULTIUSER) */

static mr_dentry_t *fs_lookup(mr_fs_struct_t *fs_struct, const char *path) {
    mr_dentry_t *start;

    /* Absolute: start at root; relative: start at cwd */
    start = (path[0] == '/') ? fs_struct->root : fs_struct->cwd;
    return mr_dentry_lookup(start, fs_struct->root, "%s", path);
}

static mr_dentry_t *fs_mkpath(mr_fs_struct_t *fs_struct, const char *path) {
    mr_dentry_t *start;

    /* Absolute: start at root; relative: start at cwd */
    start = (path[0] == '/') ? fs_struct->root : fs_struct->cwd;
    return mr_dentry_mkpath(start, fs_struct->root, "%s", path);
}

mr_err_t mr_fs_mount(const char *source, const char *path, const char *fstype,
                     int flags, void *data) {
    mr_bool_t mnt_created, mnt_held;
    mr_fs_struct_t *fs_struct;
    mr_file_system_t *fs;
    mr_super_block_t *sb;
    mr_dentry_t *mnt;
    mr_err_t ret;

    /* Check parameter */
    MR_ASSERT((fstype != MR_NULL) && (fstype[0] != '\0'));
    MR_ASSERT((path != MR_NULL) && (path[0] != '\0'));

    /* Get current file-system structure */
    fs_struct = mr_fs_current();
    if (!fs_struct) {
        return -MR_EINVAL;
    }

    /* Find FS */
    fs = mr_file_system_find(fstype);
    if (!fs) {
        return -MR_ENOENT;
    }

    /* Resolve or create the mount-point dentry */
    if ((path[0] != '/') || (path[1] != '\0')) {
        /* Mounting a subdirectory */
        mnt = fs_lookup(fs_struct, path);
        if (!mnt) {
            /* Create the mount-point dentry path */
            mnt = fs_mkpath(fs_struct, path);
            if (!mnt) {
                /* Put FS */
                mr_file_system_put(fs);
                return -MR_ENOMEM;
            }
            mnt_created = MR_TRUE;
        } else {
            mnt_created = MR_FALSE;
        }
        mnt_held = MR_TRUE;
    } else {
        /* Mounting the root */
        mnt_created = MR_FALSE;
        mnt = fs_struct->root;
        mnt_held = MR_FALSE;
    }

    /* Mount-point must be a bare dentry */
    if (mnt->inode) {
        /* Repeated mount */
        mr_file_system_put(fs);
        ret = -MR_EBUSY;
        goto _exit;
    }

    /* Create and mount the super-block */
    sb = mr_super_block_create(fs, source, flags, data);
    mr_file_system_put(fs);
    if (!sb) {
        ret = -MR_EIO;
        goto _exit;
    }

    /* Bind the mount-point to the sb's root inode */
    ret = mr_dentry_instantiate(mnt, sb->root);
    if (ret != 0) {
        mr_super_block_del(sb);
        goto _exit;
    }

    /* Track the mount-point in the sb */
    sb->mnt = mnt;

    /* Put dentry */
    if (mnt_held) {
        mr_dentry_put(mnt);
    }
    return 0;
_exit:
    /* Tear down a dentry we just created */
    if (mnt_created) {
        mr_dentry_del(mnt);
    }

    /* Put dentry */
    if (mnt_held) {
        mr_dentry_put(mnt);
    }
    return ret;
}

static void fs_purge_children(mr_dentry_t *dentry) {
    mr_dentry_t *child;

    /* Tear down the subtree under dentry in post-order: purge each
     * child's descendants first, then release the child. mr_dentry_del()
     * drops the creation reference, and the matching mr_dentry_put()
     * drops the reference taken by mr_dentry_child_get() */
    while (!mr_dentry_is_empty(dentry)) {
        /* Get the first child */
        child = mr_dentry_child_get(dentry, 0);
        if (!child) {
            break;
        }

        /* Purge its subtree first */
        fs_purge_children(child);

        /* Delete the child */
        mr_dentry_del(child);

        /* Put dentry */
        mr_dentry_put(child);
    }
}

mr_err_t mr_fs_umount(const char *path) {
    mr_fs_struct_t *fs_struct;
    mr_super_block_t *sb;
    mr_dentry_t *mnt;
    mr_err_t ret;

    /* Check parameter */
    MR_ASSERT((path != MR_NULL) && (path[0] != '\0'));

    /* Get current file-system structure */
    fs_struct = mr_fs_current();
    if (!fs_struct) {
        return -MR_EINVAL;
    }

    /* Resolve the mount-point dentry */
    if ((path[0] != '/') || (path[1] != '\0')) {
        /* Unmounting a subdirectory */
        mnt = fs_lookup(fs_struct, path);
        if (!mnt) {
            return -MR_ENOENT;
        }
    } else {
        /* Unmounting the root */
        mnt = fs_struct->root;
    }

    /* Find the super-block mounted at this path */
    sb = mr_super_block_find_by_mnt(mnt);
    if (!sb) {
        /* Not mounted */
        if (mnt != fs_struct->root) {
            mr_dentry_put(mnt);
        }
        return -MR_ENOENT;
    }

    /* Invisible from the tree */
    if (mnt != fs_struct->root) {
        mr_dentry_remove(mnt);
    }

    /* Detach the mount-point from the super-block */
    mr_super_block_detach_mnt(sb);

    /* Tear down the mounted subtree */
    fs_purge_children(mnt);

    /* Delete the super-block(kill_sb drops the root inode's creation
     * reference, while open files may keep inode/sb references alive) */
    ret = mr_super_block_del(sb);
    mr_super_block_put(sb);

    /* Delete the mount-point dentry unless it is the global root. Done
     * unconditionally so a super-block deletion failure can not strand the
     * already-detached mount-point dentry */
    if (mnt != fs_struct->root) {
        mr_dentry_del(mnt);
        mr_dentry_put(mnt);
    } else {
        /* Static root: just drop the instantiate reference */
        mr_inode_put(mnt->inode);
        mnt->inode = MR_NULL;
    }
    return ret;
}

static mr_err_t fs_split_path(const char *path, fs_path_split_t *split) {
    mr_size_t len, i, parent_len;
    mr_bool_t found;

    /* Init split */
    split->parent = split->ipath;
    split->leaf = MR_NULL;

    /* Get path length */
    len = mr_strlen(path);
    if (len == 0) {
        return -MR_EINVAL;
    }

    /* Find the last '/' */
    for (found = MR_FALSE, parent_len = 0, i = 0; i < len; i++) {
        if (path[i] == '/') {
            found = MR_TRUE;
            parent_len = i;
        }
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
        /* No '/': parent is root */
        split->ipath[0] = '/';
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

static mr_err_t fs_create_node(mr_dentry_t *parent, const char *name,
                               mr_inode_type_t type, mr_dentry_t **dentry) {
    mr_dentry_t *child, *existing;
    const mr_dentry_ops_t *dops;
    mr_inode_t *inode;
    mr_err_t ret;

    /* Check if the parent is a directory */
    if ((!parent->inode) || (parent->inode->type != MR_INODE_DIR)) {
        return -MR_ENOTDIR;
    }
    /* Check the FS supports creation */
    if ((!parent->inode->ops) || (!parent->inode->ops->create)) {
        return -MR_ENOSYS;
    }

    /* Check if the name is existing */
    existing = mr_dentry_lookup(parent, parent, "%s", name);
    if (existing) {
        /* Repeated create */
        mr_dentry_put(existing);
        return -MR_EEXIST;
    }

    /* Create inode */
    ret = parent->inode->ops->create(parent->inode, name, type, &inode);
    if (ret != 0) {
        return ret;
    }

    /* Inherit the parent super-block */
    if (parent->inode->sb) {
        inode->sb = mr_super_block_get(parent->inode->sb);
        if (!inode->sb) {
            mr_inode_put(inode);
            return -MR_ENOENT;
        }
    }

    /* Create the dentry(FS dentries inherit the super-block) */
    dops = parent->inode->sb ? parent->inode->sb->dops : MR_NULL;
    child = mr_dentry_create(dops, "%s", name);
    if (!child) {
        mr_inode_put(inode);
        return -MR_ENOMEM;
    }

    /* Bind the inode before adding to the tree, so a concurrent lookup never
     * observes a tree dentry without an inode */
    ret = mr_dentry_instantiate(child, inode);
    mr_inode_put(inode);
    if (ret != 0) {
        mr_dentry_del(child);
        return ret;
    }

    /* Add to the tree */
    ret = mr_dentry_add(child, parent);
    if (ret != 0) {
        mr_dentry_del(child);
        return ret;
    }

    /* Get dentry */
    *dentry = mr_dentry_get(child);
    return 0;
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

    /* The inode must provide file operations */
    if (!inode->fops) {
        return -MR_ENOSYS;
    }

    /* O_TRUNC applies to a writable regular file only. Fail fast when the FS
     * can not truncate, before any state is touched */
    trunc = ((flags & MR_O_TRUNC) != 0) && (inode->type == MR_INODE_REG)
         && ((flags & MR_O_ACCMODE) != MR_O_RDONLY);
    if (trunc && ((!inode->ops) || (!inode->ops->truncate))) {
        return -MR_ENOSYS;
    }

    /* Pin the dentry/inode for the open file's lifetime. The caller drops its
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
    if (inode->fops->open) {
        ret = inode->fops->open(inode, file);
        if (ret != 0) {
            file->fops = MR_NULL;
            goto _exit;
        }
    }

    /* O_TRUNC: now that the file is really open, shrink it to zero length */
    if (trunc) {
        ret = inode->ops->truncate(inode, 0);
        if (ret != 0) {
            goto _exit;
        }
    }

    /* O_APPEND: position at end-of-file (after any truncation) */
    if (flags & MR_O_APPEND) {
        file->pos = inode->size;
    }
    return 0;
_exit:
    /* Close file */
    fs_file_close(file);
    return ret;
}

mr_err_t mr_fs_open(mr_file_t *file, const char *path, int flags) {
    mr_dentry_t *dentry, *parent;
    mr_fs_struct_t *fs_struct;
    fs_path_split_t split;
    mr_inode_t *inode;
    mr_err_t ret;

    /* Check parameter */
    MR_ASSERT((path != MR_NULL) && (path[0] != '\0'));
    MR_ASSERT(file != MR_NULL);

    /* The file must not already be open */
    if (file->fops) {
        return -MR_EBUSY;
    }

    /* Validate the access mode (only RDONLY / WRONLY / RDWR) */
    if ((flags & MR_O_ACCMODE) > MR_O_RDWR) {
        return -MR_EINVAL;
    }

    /* Get current file-system structure */
    fs_struct = mr_fs_current();
    if (!fs_struct) {
        return -MR_EINVAL;
    }

    /* Try to look up the existing node */
    dentry = fs_lookup(fs_struct, path);
    if (dentry) {
        /* O_CREAT|O_EXCL: the file must not already exist */
        if ((flags & MR_O_CREAT) && (flags & MR_O_EXCL)) {
            mr_dentry_put(dentry);
            return -MR_EEXIST;
        }
    } else {
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
        parent = fs_lookup(fs_struct, split.parent);
        if (!parent) {
            /* Parent not found */
            fs_split_path_free(&split);
            return -MR_ENOENT;
        }

        /* Create the node */
        ret = fs_create_node(parent, split.leaf, MR_INODE_REG, &dentry);
        fs_split_path_free(&split);
        mr_dentry_put(parent);
        if (ret != 0) {
            return ret;
        }
    }

    /* The node must have an inode */
    inode = dentry->inode;
    if (!inode) {
        mr_dentry_put(dentry);
        return -MR_ENOENT;
    }

    /* A directory can only be opened read-only */
    if ((inode->type == MR_INODE_DIR)
        && ((flags & MR_O_ACCMODE) != MR_O_RDONLY)) {
        mr_dentry_put(dentry);
        return -MR_EISDIR;
    }

    /* O_DIRECTORY: the node must be a directory */
    if ((flags & MR_O_DIRECTORY) && (inode->type != MR_INODE_DIR)) {
        mr_dentry_put(dentry);
        return -MR_ENOTDIR;
    }

    /* Open file */
    ret = fs_file_open(file, dentry, inode, flags);
    mr_dentry_put(dentry);
    return ret;
}

mr_err_t mr_fs_close(mr_file_t *file) {
    /* Check parameter */
    MR_ASSERT(file != MR_NULL);

    /* The file must be open */
    if (!file->fops) {
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

mr_err_t mr_fs_closedir(mr_file_t *file) {
    /* Close the directory handle */
    return mr_fs_close(file);
}

mr_bool_t mr_fs_readdir(mr_file_t *file, mr_dirent_t *dirent) {
    /* Check parameter */
    MR_ASSERT(file != MR_NULL);
    MR_ASSERT(dirent != MR_NULL);

    /* The file must be open and support directory reading */
    if ((!file->fops) || (!file->fops->readdir)) {
        return MR_FALSE;
    }

    /* Only a directory can be read */
    if ((!file->inode) || (file->inode->type != MR_INODE_DIR)) {
        return MR_FALSE;
    }

    /* Read the next entry; FALSE means end-of-directory */
    return file->fops->readdir(file, dirent);
}

mr_err_t mr_fs_stat(const char *path, mr_stat_t *stat) {
    mr_fs_struct_t *fs_struct;
    mr_dentry_t *dentry;
    mr_inode_t *inode;
    mr_err_t ret;

    /* Check parameter */
    MR_ASSERT((path != MR_NULL) && (path[0] != '\0'));
    MR_ASSERT(stat != MR_NULL);

    /* Get current file-system structure */
    fs_struct = mr_fs_current();
    if (!fs_struct) {
        return -MR_EINVAL;
    }

    /* Look up the node */
    dentry = fs_lookup(fs_struct, path);
    if (!dentry) {
        return -MR_ENOENT;
    }

    /* The node must have an inode */
    inode = dentry->inode;
    if (!inode) {
        ret = -MR_ENOENT;
        goto _exit;
    }

    /* Fill generic inode status */
    mr_memset(stat, 0, sizeof(*stat));
    stat->type = inode->type;
    stat->mode = inode->mode;
    stat->nlink = inode->nlink;
    stat->size = inode->size;

    /* Let the FS override dynamic fields if it provides a hook */
    if ((inode->ops) && (inode->ops->stat)) {
        ret = inode->ops->stat(inode, stat);
        if (ret != 0) {
            goto _exit;
        }
    }
    ret = 0;
_exit:
    /* Put dentry */
    mr_dentry_put(dentry);
    return ret;
}

mr_err_t mr_fs_unlink(const char *path) {
    mr_dentry_t *dentry, *parent;
    mr_fs_struct_t *fs_struct;
    mr_err_t ret;

    /* Check parameter */
    MR_ASSERT((path != MR_NULL) && (path[0] != '\0'));

    /* Get current file-system structure */
    fs_struct = mr_fs_current();
    if (!fs_struct) {
        return -MR_EINVAL;
    }

    /* Look up the target node. A non-directory mid-path component also fails
     * here as -MR_ENOENT (not -MR_ENOTDIR); matches mr_fs_open */
    dentry = fs_lookup(fs_struct, path);
    if (!dentry) {
        return -MR_ENOENT;
    }

    /* The node must have an inode(a negative dentry is not a real file) */
    if (!dentry->inode) {
        ret = -MR_ENOENT;
        goto _exit;
    }

    /* A mount-point directory can not be removed */
    if (dentry->inode->sb && (dentry->inode->sb->mnt == dentry)) {
        ret = -MR_EBUSY;
        goto _exit;
    }

    /* The root has no parent and can not be unlinked */
    parent = dentry->upper;
    if (!parent) {
        ret = -MR_EBUSY;
        goto _exit;
    }

    /* Check the parent is a directory */
    if ((!parent->inode) || (parent->inode->type != MR_INODE_DIR)) {
        ret = -MR_ENOTDIR;
        goto _exit;
    }

    /* A non-empty directory can not be removed */
    if ((dentry->inode->type == MR_INODE_DIR)
        && (!mr_dentry_is_empty(dentry))) {
        ret = -MR_ENOTEMPTY;
        goto _exit;
    }

    /* Check the FS supports unlink */
    if ((!parent->inode->ops) || (!parent->inode->ops->unlink)) {
        ret = -MR_ENOSYS;
        goto _exit;
    }

    /* Ask the FS to remove the name from the directory. On failure the dentry
     * is left intact in the tree */
    ret = parent->inode->ops->unlink(parent->inode, MR_DENTRY_NAME(dentry));
    if (ret != 0) {
        goto _exit;
    }

    /* Detach the dentry from the tree and drop the creation reference. An open
     * file still keeps the inode/dentry alive until it is closed */
    mr_dentry_del(dentry);
_exit:
    mr_dentry_put(dentry);
    return ret;
}

mr_err_t mr_fs_rename(const char *old_path, const char *new_path) {
    mr_dentry_t *new_parent, *existing, *p;
    mr_dentry_t *old_dentry, *old_parent;
    mr_fs_struct_t *fs_struct;
    fs_path_split_t split;
    mr_err_t ret;

    /* Check parameter */
    MR_ASSERT((old_path != MR_NULL) && (old_path[0] != '\0'));
    MR_ASSERT((new_path != MR_NULL) && (new_path[0] != '\0'));

    /* Get current file-system structure */
    fs_struct = mr_fs_current();
    if (!fs_struct) {
        return -MR_EINVAL;
    }

    /* Look up the source node */
    old_dentry = fs_lookup(fs_struct, old_path);
    if ((!old_dentry) || (!old_dentry->inode)) {
        ret = -MR_ENOENT;
        goto _exit;
    }

    /* The root has no parent and can not be renamed */
    old_parent = old_dentry->upper;
    if (!old_parent) {
        ret = -MR_EBUSY;
        goto _exit;
    }

    /* The source parent must be a directory */
    if ((!old_parent->inode) || (old_parent->inode->type != MR_INODE_DIR)) {
        ret = -MR_ENOTDIR;
        goto _exit;
    }

    /* A mount-point directory can not be renamed */
    if (old_dentry->inode->sb && (old_dentry->inode->sb->mnt == old_dentry)) {
        ret = -MR_EBUSY;
        goto _exit;
    }

    /* Split the destination into parent + leaf */
    ret = fs_split_path(new_path, &split);
    if (ret != 0) {
        goto _exit;
    }

    /* Look up the destination parent directory */
    new_parent = fs_lookup(fs_struct, split.parent);
    if ((!new_parent) || (!new_parent->inode)) {
        ret = -MR_ENOENT;
        goto _exit_free;
    }

    /* The destination parent must be a directory */
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

    /* Reject renaming a directory into its own subtree */
    for (p = new_parent; p; p = p->upper) {
        if (p == old_dentry) {
            ret = -MR_EINVAL;
            goto _exit_put;
        }
    }

    /* The destination must not already exist */
    existing = mr_dentry_lookup(new_parent, new_parent, "%s", split.leaf);
    if (existing) {
        if (existing == old_dentry) {
            /* Renaming a node to itself is a no-op */
            mr_dentry_put(existing);
            ret = 0;
            goto _exit_put;
        }
        mr_dentry_put(existing);
        ret = -MR_EEXIST;
        goto _exit_put;
    }

    /* Check the FS supports rename */
    if ((!old_parent->inode->ops) || (!old_parent->inode->ops->rename)) {
        ret = -MR_ENOSYS;
        goto _exit_put;
    }

    /* Ask the FS to rename the node. On failure the dentry tree is untouched */
    ret = old_parent->inode->ops->rename(old_parent->inode,
                                         MR_DENTRY_NAME(old_dentry),
                                         new_parent->inode, split.leaf);
    if (ret != 0) {
        goto _exit_put;
    }

    /* Update the dentry tree to match the new name and parent */
    ret = mr_dentry_move(old_dentry, new_parent, split.leaf);
_exit_put:
    mr_dentry_put(new_parent);
_exit_free:
    fs_split_path_free(&split);
_exit:
    if (old_dentry) {
        mr_dentry_put(old_dentry);
    }
    return ret;
}

mr_err_t mr_fs_mkdir(const char *path) {
    mr_dentry_t *parent, *dentry;
    mr_fs_struct_t *fs_struct;
    fs_path_split_t split;
    mr_err_t ret;

    /* Check parameter */
    MR_ASSERT((path != MR_NULL) && (path[0] != '\0'));

    /* Get current file-system structure */
    fs_struct = mr_fs_current();
    if (!fs_struct) {
        return -MR_EINVAL;
    }

    /* Split path into parent + leaf */
    ret = fs_split_path(path, &split);
    if (ret != 0) {
        return ret;
    }

    /* Look up the parent directory */
    parent = fs_lookup(fs_struct, split.parent);
    if (!parent) {
        /* Parent not found */
        fs_split_path_free(&split);
        return -MR_ENOENT;
    }

    /* Create the directory node */
    ret = fs_create_node(parent, split.leaf, MR_INODE_DIR, &dentry);
    fs_split_path_free(&split);
    mr_dentry_put(parent);
    if (ret == 0) {
        /* Drop the reference acquired by fs_create_node */
        mr_dentry_put(dentry);
    }
    return ret;
}

mr_ssize_t mr_fs_read(mr_file_t *file, void *buf, mr_size_t size) {
    /* Check parameter */
    MR_ASSERT((buf != MR_NULL) || (size == 0));
    MR_ASSERT(file != MR_NULL);

    /* The file must be open */
    if (!file->fops) {
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
    if (!file->fops->read) {
        return -MR_ENOSYS;
    }

    /* Read from the file */
    return file->fops->read(file, buf, size);
}

mr_ssize_t mr_fs_write(mr_file_t *file, const void *buf, mr_size_t size) {
    /* Check parameter */
    MR_ASSERT((buf != MR_NULL) || (size == 0));
    MR_ASSERT(file != MR_NULL);

    /* The file must be open */
    if (!file->fops) {
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
    if (!file->fops->write) {
        return -MR_ENOSYS;
    }

    /* Write to the file */
    return file->fops->write(file, buf, size);
}

mr_int64_t mr_fs_seek(mr_file_t *file, mr_int64_t offset, int whence) {
    mr_int64_t base;

    /* Check parameter */
    MR_ASSERT(file != MR_NULL);

    /* The file must be open */
    if (!file->fops) {
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

    /* Set position(clamped to >= 0) */
    file->pos = base + offset;
    if (file->pos < 0) {
        file->pos = 0;
    }
    return file->pos;
}

mr_int64_t mr_fs_tell(mr_file_t *file) {
    /* Check parameter */
    MR_ASSERT(file != MR_NULL);

    /* The file must be open */
    if (!file->fops) {
        return -MR_EBADF;
    }

    /* Return the current position */
    return file->pos;
}

mr_err_t mr_fs_ioctl(mr_file_t *file, int cmd, void *arg) {
    /* Check parameter */
    MR_ASSERT(file != MR_NULL);

    /* The file must be open */
    if (!file->fops) {
        return -MR_EBADF;
    }

    /* Check the FS supports ioctl */
    if ((!file->fops) || (!file->fops->ioctl)) {
        return -MR_ENOSYS;
    }

    /* Control the file */
    return file->fops->ioctl(file, cmd, arg);
}

mr_err_t mr_fs_truncate(mr_file_t *file, mr_int64_t length) {
    /* Check parameter */
    MR_ASSERT(file != MR_NULL);

    /* The file must be open */
    if (!file->fops) {
        return -MR_EBADF;
    }

    /* Truncation applies to regular files only */
    if (file->inode->type == MR_INODE_DIR) {
        return -MR_EISDIR;
    }
    if (file->inode->type != MR_INODE_REG) {
        return -MR_EINVAL;
    }

    /* The file must be open for writing */
    if ((file->flags & MR_O_ACCMODE) == MR_O_RDONLY) {
        return -MR_EBADF;
    }

    /* The length must be valid */
    if (length < 0) {
        return -MR_EINVAL;
    }

    /* Check the FS supports truncation */
    if ((!file->inode->ops) || (!file->inode->ops->truncate)) {
        return -MR_ENOSYS;
    }

    /* Truncate the file */
    return file->inode->ops->truncate(file->inode, length);
}

mr_err_t mr_fs_sync(mr_file_t *file) {
    /* Check parameter */
    MR_ASSERT(file != MR_NULL);

    /* The file must be open */
    if (!file->fops) {
        return -MR_EBADF;
    }

    /* Check the FS supports sync */
    if ((!file->fops) || (!file->fops->sync)) {
        return -MR_ENOSYS;
    }

    /* Sync the file */
    return file->fops->sync(file);
}

mr_err_t mr_fs_struct_init(mr_fs_struct_t *fs_struct) {
    MR_ASSERT(fs_struct != MR_NULL);

    /* Init FS structure */
    fs_struct->root = mr_dentry_get(&__fs_root);
    if (!fs_struct->root) {
        return -MR_EINVAL;
    }
    fs_struct->cwd = mr_dentry_get(&__fs_root);
    if (!fs_struct->cwd) {
        mr_dentry_put(fs_struct->root);
        return -MR_EINVAL;
    }
    return 0;
}

mr_err_t mr_fs_chroot(const char *path) {
    mr_fs_struct_t *fs_struct;
    mr_dentry_t *dentry;
    mr_inode_t *inode;

    /* Check parameter */
    MR_ASSERT((path != MR_NULL) && (path[0] != '\0'));

    /* Get current file-system structure */
    fs_struct = mr_fs_current();
    if (!fs_struct) {
        return -MR_EINVAL;
    }

    /* Look up the new root */
    dentry = fs_lookup(fs_struct, path);
    if (!dentry) {
        return -MR_ENOENT;
    }

    /* The new root must be a directory */
    inode = dentry->inode;
    if ((!inode) || (inode->type != MR_INODE_DIR)) {
        mr_dentry_put(dentry);
        return -MR_ENOTDIR;
    }

    /* Swap the context root */
    mr_dentry_put(fs_struct->root);
    fs_struct->root = dentry;
    return 0;
}

mr_err_t mr_fs_chdir(const char *path) {
    mr_fs_struct_t *fs_struct;
    mr_dentry_t *dentry;
    mr_inode_t *inode;

    /* Check parameter */
    MR_ASSERT((path != MR_NULL) && (path[0] != '\0'));

    /* Get current file-system structure */
    fs_struct = mr_fs_current();
    if (!fs_struct) {
        return -MR_EINVAL;
    }

    /* Look up the new working directory */
    dentry = fs_lookup(fs_struct, path);
    if (!dentry) {
        return -MR_ENOENT;
    }

    /* The new working directory must be a directory */
    inode = dentry->inode;
    if ((!inode) || (inode->type != MR_INODE_DIR)) {
        mr_dentry_put(dentry);
        return -MR_ENOTDIR;
    }

    /* Swap the context cwd */
    mr_dentry_put(fs_struct->cwd);
    fs_struct->cwd = dentry;
    return 0;
}

mr_ssize_t mr_fs_getcwd(char *buf, mr_size_t size) {
    mr_fs_struct_t *fs = mr_fs_current();
    mr_dentry_t *dentry;
    mr_size_t len, pos, name_len;

    /* Check parameter */
    MR_ASSERT((buf != MR_NULL) && (size > 0));

    dentry = fs->cwd;

    /* The cwd is the root */
    if (dentry == fs->root) {
        if (size < 2) {
            return -MR_ERANGE;
        }
        buf[0] = '/';
        buf[1] = '\0';
        return 1;
    }

    /* First pass: compute the path length(excluding '\0') */
    len = 0;
    while (dentry != fs->root) {
        if (!dentry->upper) {
            /* The cwd is not under the root */
            return -MR_ENOENT;
        }
        name_len = MR_DENTRY_NAME_LEN(dentry);
        len += name_len + 1u;
        dentry = dentry->upper;
    }

    /* Check the buffer can hold the path and the terminator */
    if ((len + 1u) > size) {
        return -MR_ERANGE;
    }

    /* Second pass: write the path backwards from the end */
    dentry = fs->cwd;
    pos = len;
    buf[pos] = '\0';
    while (dentry != fs->root) {
        name_len = MR_DENTRY_NAME_LEN(dentry);

        pos -= name_len;
        mr_strncpy(&buf[pos], MR_DENTRY_NAME(dentry), name_len);
        pos -= 1u;
        buf[pos] = '/';
        dentry = dentry->upper;
    }
    return (mr_ssize_t)len;
}

#if !defined(MR_USE_FS_MULTIUSER)
mr_fs_struct_t *mr_fs_current(void) {
    /* Return the single user FS structure */
    return &__fs_struct;
}
#endif /* !defined(MR_USE_FS_MULTIUSER) */

void mr_fs_init(void) {
#if !defined(MR_USE_FS_MULTIUSER)
    /* Init single user FS structure */
    mr_fs_struct_init(&__fs_struct);
#endif /* !defined(MR_USE_FS_MULTIUSER) */
}
#endif /* defined(MR_USE_FS) */
