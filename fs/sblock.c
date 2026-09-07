/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/fs/mr_fs_sblock.h>
#if defined(MR_USE_FS)
#include <mrX/fs/mr_fs_struct.h>
#include <mrX/libc/mr_malloc.h>

/* Super-block list */
static mr_sblock_t *__fs_sb_list = MR_NULL;

/* Super-block clazzes(dynamic) */
static void sb_dynamic_release(mr_object_t *obj);
MR_CLAZZES_DEFINE(sblock, MR_NULL, sb_dynamic_release, MR_NULL);

static void sb_list_add(mr_sblock_t *sb) {
    int mask;

    /* Lock */
    mask = mr_fs_struct_lock();

    /* Add super-block to the list */
    sb->next = __fs_sb_list;
    __fs_sb_list = sb;

    /* Unlock */
    mr_fs_struct_unlock(mask);
}

static void sb_root_put(mr_sblock_t *sb) {
    mr_inode_t *inode;

    /* Put root inode */
    inode = sb->root;
    sb->root = MR_NULL;
    if (inode) {
        mr_inode_put(inode);
    }
}

static mr_err_t sb_init(mr_sblock_t *sb, const mr_clazz_t *clazz,
                        mr_file_system_t *type, const char *source, int flags,
                        void *data) {
    mr_err_t ret;

    /* Bind static file-system descriptor */
    sb->type = type;
    if (!sb->type->mount) {
        return -MR_EIO;
    }

    /* Init super-block */
    sb->mnt = MR_NULL;
    sb->root_dentry = MR_NULL;
    sb->root = MR_NULL;
    sb->next = MR_NULL;
    sb->mount_parent = MR_NULL;
    sb->mount_child = MR_NULL;
    sb->mount_next = MR_NULL;
    sb->move_busy = MR_FALSE;
    sb->state = MR_SBLOCK_STATE_INACTIVE;
    sb->op_cnt = 0;
    sb->open_cnt = 0;
    sb->flags = flags;
    sb->dops = MR_NULL;
    sb->priv = MR_NULL;

    /* Let the FS mount and fill the super-block. On failure the FS cleans up
     * its own resources, so the sb is freed without unmount */
    ret = sb->type->mount(sb, source, flags, data);
    if (ret != 0) {
        goto _exit;
    }

    /* Validate the root inode: must exist and be a directory */
    if ((!sb->root) || (sb->root->type != MR_INODE_DIR)) {
        ret = -MR_ENOTDIR;
        goto _unmount_exit;
    }

    /* Init object. This is the last step that can fail: once the object is
     * live, the caller must not free it without going through the object
     * release, so nothing below may jump to the error path */
    ret = mr_object_init((mr_object_t *)sb, clazz);
    if (ret != 0) {
        goto _unmount_exit;
    }

    /* Back-link root inode to the super-block */
    sb->root->sb = mr_sblock_get(sb);

    /* Add super-block to the sb list */
    sb_list_add(sb);
    return 0;
_unmount_exit:
    /* Put super-block root */
    sb_root_put(sb);

    /* Unmount the initialized backend */
    if (sb->type->unmount) {
        sb->type->unmount(sb);
    }
_exit:
    sb->type = MR_NULL;
    return ret;
}

mr_err_t mr_sblock_create(mr_file_system_t *type, const char *source, int flags,
                          void *data, mr_sblock_t **out) {
    const mr_clazz_t *clazz = MR_CLAZZES_DYNAMIC(sblock);
    mr_sblock_t *sb;
    mr_err_t ret;

    /* Check parameter */
    MR_ASSERT(type != MR_NULL);
    MR_ASSERT(type->mount != MR_NULL);
    MR_ASSERT(out != MR_NULL);

    /* Init output */
    *out = MR_NULL;

    /* Allocate super-block */
    sb = mr_malloc(sizeof(mr_sblock_t));
    if (!sb) {
        return -MR_ENOMEM;
    }

    /* Init super-block */
    ret = sb_init(sb, clazz, type, source, flags, data);
    if (ret != 0) {
        mr_free(sb);
        return ret;
    }
    *out = sb;
    return 0;
}

static mr_err_t sb_list_remove(mr_sblock_t *sb) {
    mr_sblock_t **link;
    int mask;

    /* Lock */
    mask = mr_fs_struct_lock();

    /* Find the added entry by address. A never-added object may have an
     * arbitrary next pointer, so do not trust sb->next here */
    for (link = &__fs_sb_list; *link; link = &(*link)->next) {
        if (*link == sb) {
            break;
        }
    }

    /* Not registered */
    if (!(*link)) {
        /* Unlock */
        mr_fs_struct_unlock(mask);
        return -MR_ENOENT;
    }

    /* Remove from the list */
    *link = sb->next;
    sb->next = MR_NULL;

    /* Unlock */
    mr_fs_struct_unlock(mask);
    return 0;
}

mr_err_t mr_sblock_del(mr_sblock_t *sb) {
    mr_err_t ret;

    /* Check parameter */
    MR_ASSERT((sb != MR_NULL) && MR_OBJECT_IS_INITED(sb));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(sb, sblock));

    /* Remove super-block */
    ret = sb_list_remove(sb);
    if (ret != 0) {
        return ret;
    }

    /* Put super-block root */
    sb_root_put(sb);

    /* Delete object */
    mr_object_del((mr_object_t *)sb);
    return 0;
}

mr_err_t mr_sblock_attach(mr_sblock_t *sb, mr_dentry_t *mnt,
                          mr_dentry_t *root) {
    mr_dentry_t *mnt_ref, *root_ref;
    mr_sblock_t *mount_parent;
    mr_err_t ret;
    int mask;

    /* Check parameter */
    MR_ASSERT((sb != MR_NULL) && MR_OBJECT_IS_INITED(sb));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(sb, sblock));
    MR_ASSERT((mnt != MR_NULL) && MR_OBJECT_IS_INITED(mnt));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(mnt, dentry));
    MR_ASSERT((root != MR_NULL) && MR_OBJECT_IS_INITED(root));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(root, dentry));

    /* Pin structure dentries */
    root_ref = mr_dentry_get(root);
    mnt_ref = mr_dentry_get(mnt);
    if ((!root_ref) || (!mnt_ref)) {
        if (root_ref) {
            mr_dentry_put(root_ref);
        }
        if (mnt_ref) {
            mr_dentry_put(mnt_ref);
        }
        return -MR_ENOENT;
    }

    /* Lock */
    mask = mr_fs_struct_lock();

    /* Attach only a newly created, inactive super-block */
    if ((sb->state != MR_SBLOCK_STATE_INACTIVE) || sb->mnt || sb->root_dentry) {
        ret = (sb->state == MR_SBLOCK_STATE_OFFLINE) ? -MR_ENODEV : -MR_EBUSY;
        goto _exit;
    }

    /* Check mounted root */
    if ((!root->inode) || (root->inode != sb->root)
        || (root->inode->type != MR_INODE_DIR)) {
        ret = -MR_EINVAL;
        goto _exit;
    }

    if (root != mnt) {
        /* A nested mount covers an existing directory */
        if ((mnt->state == MR_DENTRY_STATE_DEAD)
            || (!MR_DENTRY_IS_ADDED(mnt))) {
            ret = -MR_ENOENT;
            goto _exit;
        }
        if ((mnt->state != MR_DENTRY_STATE_ACTIVE)
            && (mnt->state != MR_DENTRY_STATE_MOUNTING)) {
            ret = -MR_EBUSY;
            goto _exit;
        }
        if ((!mnt->inode) || (mnt->inode->type != MR_INODE_DIR)) {
            ret = -MR_ENOTDIR;
            goto _exit;
        }
        if (mnt->mounted || root->upper || MR_DENTRY_IS_ADDED(root)) {
            ret = -MR_EBUSY;
            goto _exit;
        }
        mount_parent = mnt->inode->sb;
        if (!mount_parent) {
            ret = -MR_EBUSY;
            goto _exit;
        }
        if (mount_parent->state == MR_SBLOCK_STATE_OFFLINE) {
            ret = -MR_ENODEV;
            goto _exit;
        }
        if (mount_parent->state != MR_SBLOCK_STATE_ACTIVE) {
            ret = -MR_EBUSY;
            goto _exit;
        }

        /* Connect the mounted root to the visible structure. It is not added
         * to the parent's child list; the covered dentry remains there */
        root->upper = mnt->upper ? mr_dentry_get(mnt->upper) : MR_NULL;
        mnt->mounted = root;
    } else {
        /* Global root is mounted in place */
        mount_parent = MR_NULL;
        root->ops = sb->dops;
    }

    /* Attach super-block and publish it as ACTIVE */
    sb->mount_parent = mount_parent;
    sb->root_dentry = root_ref;
    sb->mnt = mnt_ref;
    if (mount_parent) {
        sb->mount_next = mount_parent->mount_child;
        mount_parent->mount_child = sb;
    }
    sb->state = MR_SBLOCK_STATE_ACTIVE;
    ret = 0;
_exit:
    /* Unlock */
    mr_fs_struct_unlock(mask);

    /* Put unused pins */
    if (ret != 0) {
        mr_dentry_put(root_ref);
        mr_dentry_put(mnt_ref);
    }
    return ret;
}

mr_err_t mr_sblock_detach(mr_sblock_t *sb, mr_dentry_t **mnt,
                          mr_dentry_t **root) {
    mr_sblock_t **link;
    mr_err_t ret;
    int mask;

    /* Check parameter */
    MR_ASSERT((sb != MR_NULL) && MR_OBJECT_IS_INITED(sb));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(sb, sblock));
    MR_ASSERT((mnt != MR_NULL) && (root != MR_NULL));

    /* Lock */
    mask = mr_fs_struct_lock();

    /* Detach an active mount or a permanently offline mount */
    if ((sb->state != MR_SBLOCK_STATE_ACTIVE)
        && (sb->state != MR_SBLOCK_STATE_OFFLINE)) {
        ret = -MR_ENOENT;
        goto _exit;
    }
    if ((!sb->mnt) || (!sb->root_dentry)) {
        ret = -MR_ENOENT;
        goto _exit;
    }
    if (sb->mount_child || sb->move_busy || (sb->op_cnt > 0)
        || ((sb->state == MR_SBLOCK_STATE_ACTIVE) && (sb->open_cnt > 0))) {
        ret = -MR_EBUSY;
        goto _exit;
    }

    /* Hide the mounted root. Media detach may already have cleared covering */
    if (sb->root_dentry != sb->mnt) {
        if (sb->mnt->mounted) {
            MR_ASSERT(sb->mnt->mounted == sb->root_dentry);
            sb->mnt->mounted = MR_NULL;
        }
    } else {
        /* Restore the neutral global root after unmount */
        sb->root_dentry->ops = MR_NULL;
    }

    /* Detach from the parent mount */
    if (sb->mount_parent) {
        for (link = &sb->mount_parent->mount_child; *link;
             link = &(*link)->mount_next) {
            if (*link == sb) {
                *link = sb->mount_next;
                break;
            }
        }
    }
    sb->mount_parent = MR_NULL;
    sb->mount_next = MR_NULL;

    /* Transfer dentry references to the caller */
    *root = sb->root_dentry;
    *mnt = sb->mnt;
    sb->root_dentry = MR_NULL;
    sb->mnt = MR_NULL;

    /* Normal unmount becomes inactive; media loss stays offline */
    if (sb->state == MR_SBLOCK_STATE_ACTIVE) {
        sb->state = MR_SBLOCK_STATE_INACTIVE;
    }
    ret = 0;
_exit:
    /* Unlock */
    mr_fs_struct_unlock(mask);
    return ret;
}

mr_err_t mr_sblock_op_enter(mr_sblock_t *sb) {
    mr_err_t ret;
    int mask;

    /* Check parameter */
    if (!sb) {
        return -MR_ENOENT;
    }

    /* Lock */
    mask = mr_fs_struct_lock();

    /* Enter only an active, attached mount */
    if ((sb->state == MR_SBLOCK_STATE_ACTIVE) && sb->mnt && sb->root_dentry) {
        sb->op_cnt++;
        ret = 0;
    } else if (sb->state == MR_SBLOCK_STATE_OFFLINE) {
        ret = -MR_ENODEV;
    } else {
        ret = -MR_ENOENT;
    }

    /* Unlock */
    mr_fs_struct_unlock(mask);
    return ret;
}

void mr_sblock_op_exit(mr_sblock_t *sb) {
    int mask;

    /* Check parameter */
    MR_ASSERT((sb != MR_NULL) && MR_OBJECT_IS_INITED(sb));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(sb, sblock));

    /* Lock */
    mask = mr_fs_struct_lock();

    /* Release operation lifetime */
    MR_ASSERT(sb->op_cnt > 0);
    sb->op_cnt--;

    /* Unlock */
    mr_fs_struct_unlock(mask);
}

void mr_sblock_offline_subtree(mr_sblock_t *sb) {
    mr_sblock_t *cur;
    int mask;

    /* Check parameter */
    MR_ASSERT((sb != MR_NULL) && MR_OBJECT_IS_INITED(sb));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(sb, sblock));

    /* Lock */
    mask = mr_fs_struct_lock();

    /* Iterative preorder walk; recursion is unbounded on nested mounts */
    cur = sb;
    while (1) {
        /* Mark this mount permanently offline */
        if (cur->state == MR_SBLOCK_STATE_ACTIVE) {
            cur->state = MR_SBLOCK_STATE_OFFLINE;
        }

        /* Hide the covering so new lookups cannot enter this mount */
        if (cur->mnt && cur->root_dentry && (cur->root_dentry != cur->mnt)) {
            cur->mnt->mounted = MR_NULL;
        }

        /* Descend first, or climb until the next sibling is found */
        if (cur->mount_child) {
            cur = cur->mount_child;
            continue;
        }
        while (cur != sb) {
            if (cur->mount_next) {
                cur = cur->mount_next;
                break;
            }
            cur = cur->mount_parent;
        }
        if (cur == sb) {
            break;
        }
    }

    /* Unlock */
    mr_fs_struct_unlock(mask);
}

mr_sblock_t *mr_sblock_subtree_leaf_get(mr_sblock_t *sb) {
    mr_sblock_t *leaf;
    int mask;

    /* Check parameter */
    MR_ASSERT((sb != MR_NULL) && MR_OBJECT_IS_INITED(sb));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(sb, sblock));

    /* Lock */
    mask = mr_fs_struct_lock();

    /* Walk the first-child chain to a leaf */
    for (leaf = sb; leaf->mount_child; leaf = leaf->mount_child) {
    }
    leaf = mr_sblock_get(leaf);

    /* Unlock */
    mr_fs_struct_unlock(mask);
    return leaf;
}

mr_err_t mr_sblock_file_acquire(mr_sblock_t *sb) {
    mr_err_t ret;
    int mask;

    /* Check parameter */
    if (!sb) {
        return -MR_ENOENT;
    }

    /* Lock */
    mask = mr_fs_struct_lock();

    /* Acquire only on an active, attached mount */
    if ((sb->state == MR_SBLOCK_STATE_ACTIVE) && sb->mnt && sb->root_dentry) {
        sb->open_cnt++;
        ret = 0;
    } else if (sb->state == MR_SBLOCK_STATE_OFFLINE) {
        ret = -MR_ENODEV;
    } else {
        ret = -MR_ENOENT;
    }

    /* Unlock */
    mr_fs_struct_unlock(mask);
    return ret;
}

void mr_sblock_file_release(mr_sblock_t *sb) {
    int mask;

    /* Check parameter */
    MR_ASSERT((sb != MR_NULL) && MR_OBJECT_IS_INITED(sb));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(sb, sblock));

    /* Lock */
    mask = mr_fs_struct_lock();

    /* Release open-file lifetime */
    MR_ASSERT(sb->open_cnt > 0);
    sb->open_cnt--;

    /* Unlock */
    mr_fs_struct_unlock(mask);
}

mr_sblock_t *mr_sblock_find_by_dentry(mr_dentry_t *dentry) {
    mr_sblock_t *sb;
    int mask;

    /* Check parameter */
    MR_ASSERT((dentry != MR_NULL) && MR_OBJECT_IS_INITED(dentry));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(dentry, dentry));

    /* Lock */
    mask = mr_fs_struct_lock();

    /* Find the super-block mounted at or rooted at this dentry */
    for (sb = __fs_sb_list; sb; sb = sb->next) {
        if ((sb->mnt == dentry) || (sb->root_dentry == dentry)) {
            /* Get super-block */
            sb = mr_sblock_get(sb);

            /* Unlock */
            mr_fs_struct_unlock(mask);
            return sb;
        }
    }

    /* Unlock */
    mr_fs_struct_unlock(mask);
    return MR_NULL;
}

static mr_sblock_t *sb_obj_release(mr_object_t *obj) {
    mr_sblock_t *sb;

    /* Get super-block */
    sb = MR_CONTAINER_OF(obj, mr_sblock_t, parent);

    /* Detach from the global list if still linked. This is the safety net for
     * a super-block whose last reference is dropped without going through
     * mr_sblock_del(); remove is a no-op if already detached */
    sb_list_remove(sb);

    /* Release backend resources after every inode and file is gone */
    if (sb->type && sb->type->unmount) {
        sb->type->unmount(sb);
    }
    sb->type = MR_NULL;
    return sb;
}

static void sb_dynamic_release(mr_object_t *obj) {
    mr_sblock_t *sb;

    /* Release super-block */
    sb = sb_obj_release(obj);

    /* Free super-block */
    mr_free(sb);
}
#endif /* defined(MR_USE_FS) */
