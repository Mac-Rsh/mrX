/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/fs/mr_sb.h>
#if defined(MR_USE_FS)
#include <mrX/libc/mr_malloc.h>
#include <mrX/fs/mr_nsp.h>

/* Super-block list */
static mr_super_block_t *__fs_sb_list = MR_NULL;

/* Super-block clazzes(dynamic) */
static void sb_dynamic_release(mr_object_t *obj);
MR_CLAZZES_DEFINE(superblock, MR_NULL, sb_dynamic_release, MR_NULL);

static void sb_list_add(mr_super_block_t *sb) {
    int mask;

    /* Lock */
    mask = mr_fs_nsp_lock();

    /* Add super-block to the list */
    sb->next = __fs_sb_list;
    __fs_sb_list = sb;

    /* Unlock */
    mr_fs_nsp_unlock(mask);
}

static void super_block_root_put(mr_super_block_t *sb) {
    mr_inode_t *inode;

    /* Put root inode */
    inode = sb->root;
    sb->root = MR_NULL;
    if (inode) {
        mr_inode_put(inode);
    }
}

static mr_err_t super_block_init(mr_super_block_t *sb, const mr_clazz_t *clazz,
                                 mr_file_system_t *type, const char *source,
                                 int flags, void *data) {
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
    sb->flags = flags;
    sb->dops = MR_NULL;
    sb->priv = MR_NULL;
    sb->next = MR_NULL;
    sb->mount_parent = MR_NULL;
    sb->mount_child = MR_NULL;
    sb->mount_next = MR_NULL;
    sb->op_cnt = 0;
    sb->detaching = MR_FALSE;

    /* Let the FS mount and fill the super-block. On failure the FS cleans up
     * its own resources, so the sb is freed without kill_sb */
    ret = sb->type->mount(sb, source, flags, data);
    if (ret != 0) {
        goto _exit;
    }

    /* Validate the root inode: must exist and be a directory */
    if ((!sb->root) || (sb->root->type != MR_INODE_DIR)) {
        ret = -MR_ENOTDIR;
        goto _kill_exit;
    }

    /* Init object */
    if (mr_object_init((mr_object_t *)sb, clazz) != 0) {
        ret = -MR_EINVAL;
        goto _kill_exit;
    }

    /* Back-link root inode to the super-block */
    sb->root->sb = mr_super_block_get(sb);
    if (!sb->root->sb) {
        ret = -MR_ENOENT;
        goto _kill_exit;
    }

    /* Add super-block to the sb list */
    sb_list_add(sb);
    return 0;
_kill_exit:
    /* Put super-block root */
    super_block_root_put(sb);

    /* Kill the super-block */
    if (sb->type->kill_sb) {
        sb->type->kill_sb(sb);
    }
_exit:
    sb->type = MR_NULL;
    return ret;
}

mr_err_t mr_super_block_create(mr_file_system_t *type, const char *source,
                               int flags, void *data, mr_super_block_t **out) {
    const mr_clazz_t *clazz = MR_CLAZZES_DYNAMIC(superblock);
    mr_super_block_t *sb;
    mr_err_t ret;

    /* Check parameter */
    MR_ASSERT(type != MR_NULL);
    MR_ASSERT(type->mount != MR_NULL);
    MR_ASSERT(out != MR_NULL);

    /* Init output */
    *out = MR_NULL;

    /* Allocate super-block */
    sb = mr_malloc(sizeof(mr_super_block_t));
    if (!sb) {
        return -MR_ENOMEM;
    }

    /* Init super-block */
    ret = super_block_init(sb, clazz, type, source, flags, data);
    if (ret != 0) {
        mr_free(sb);
        return ret;
    }
    *out = sb;
    return 0;
}

mr_err_t mr_super_block_detach_prepare(mr_super_block_t *sb) {
    mr_err_t ret;
    int mask;

    /* Check parameter */
    MR_ASSERT((sb != MR_NULL) && MR_OBJECT_IS_INITED(sb));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(sb, superblock));

    /* Lock */
    mask = mr_fs_nsp_lock();

    /* Check detach state */
    if (sb->detaching || (sb->op_cnt > 0) || sb->mount_child) {
        ret = -MR_EBUSY;
    } else if ((!sb->mnt) || (!sb->root_dentry)) {
        ret = -MR_ENOENT;
    } else {
        sb->detaching = MR_TRUE;
        ret = 0;
    }

    /* Unlock */
    mr_fs_nsp_unlock(mask);
    return ret;
}

void mr_super_block_detach_cancel(mr_super_block_t *sb) {
    int mask;

    /* Check parameter */
    MR_ASSERT((sb != MR_NULL) && MR_OBJECT_IS_INITED(sb));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(sb, superblock));

    /* Lock */
    mask = mr_fs_nsp_lock();

    /* Cancel detach */
    sb->detaching = MR_FALSE;

    /* Unlock */
    mr_fs_nsp_unlock(mask);
}

mr_err_t mr_super_block_op_enter(mr_super_block_t *sb) {
    mr_err_t ret;
    int mask;

    /* Check parameter */
    if (!sb) {
        return -MR_ENOENT;
    }

    /* Lock */
    mask = mr_fs_nsp_lock();

    /* Enter only an attached, active super-block */
    if ((!sb->mnt) || (!sb->root_dentry)) {
        ret = -MR_ENOENT;
    } else if (sb->detaching) {
        ret = -MR_EBUSY;
    } else {
        sb->op_cnt++;
        ret = 0;
    }

    /* Unlock */
    mr_fs_nsp_unlock(mask);
    return ret;
}

void mr_super_block_op_exit(mr_super_block_t *sb) {
    int mask;

    /* Check parameter */
    MR_ASSERT((sb != MR_NULL) && MR_OBJECT_IS_INITED(sb));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(sb, superblock));

    /* Lock */
    mask = mr_fs_nsp_lock();

    /* Leave namespace operation */
    MR_ASSERT(sb->op_cnt > 0);
    sb->op_cnt--;

    /* Unlock */
    mr_fs_nsp_unlock(mask);
}

static mr_err_t super_block_remove(mr_super_block_t *sb) {
    mr_super_block_t **link;
    int mask;

    /* Lock */
    mask = mr_fs_nsp_lock();

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
        mr_fs_nsp_unlock(mask);
        return -MR_ENOENT;
    }

    /* Remove from the list */
    *link = sb->next;
    sb->next = MR_NULL;

    /* Unlock */
    mr_fs_nsp_unlock(mask);
    return 0;
}

static void super_block_kill(mr_super_block_t *sb) {
    if (sb->type) {
        /* Kill the super-block */
        if (sb->type->kill_sb) {
            sb->type->kill_sb(sb);
        }

        /* Detach file-system descriptor */
        sb->type = MR_NULL;
    }
}

mr_err_t mr_super_block_del(mr_super_block_t *sb) {
    mr_err_t ret;

    /* Check parameter */
    MR_ASSERT((sb != MR_NULL) && MR_OBJECT_IS_INITED(sb));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(sb, superblock));

    /* Remove super-block */
    ret = super_block_remove(sb);
    if (ret != 0) {
        return ret;
    }

    /* Put super-block root */
    super_block_root_put(sb);

    /* Delete object */
    mr_object_del((mr_object_t *)sb);
    return 0;
}

mr_err_t mr_super_block_attach(mr_super_block_t *sb, mr_dentry_t *mnt,
                               mr_dentry_t *root) {
    mr_dentry_t *mnt_ref, *root_ref;
    mr_super_block_t *mount_parent;
    mr_err_t ret;
    int mask;

    /* Check parameter */
    MR_ASSERT((sb != MR_NULL) && MR_OBJECT_IS_INITED(sb));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(sb, superblock));
    MR_ASSERT((mnt != MR_NULL) && MR_OBJECT_IS_INITED(mnt));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(mnt, dentry));
    MR_ASSERT((root != MR_NULL) && MR_OBJECT_IS_INITED(root));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(root, dentry));

    /* Pin namespace dentries */
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
    mask = mr_fs_nsp_lock();

    /* Check super-block state */
    if (sb->mnt || sb->root_dentry) {
        ret = -MR_EBUSY;
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
        if ((!mnt->inode) || (mnt->inode->type != MR_INODE_DIR)) {
            ret = -MR_ENOTDIR;
            goto _exit;
        }
        if (mnt->mounted || root->upper || MR_DENTRY_IS_ADDED(root)) {
            ret = -MR_EBUSY;
            goto _exit;
        }
        mount_parent = mnt->inode->sb;
        if ((!mount_parent) || mount_parent->detaching) {
            ret = -MR_EBUSY;
            goto _exit;
        }

        /* Connect the mounted root to the visible namespace. It is not added
         * to the parent's child list; the covered dentry remains there */
        root->upper = mnt->upper ? mr_dentry_get(mnt->upper) : MR_NULL;
        mnt->mounted = root;
    } else {
        /* Global root is mounted in place */
        mount_parent = MR_NULL;
        root->ops = sb->dops;
    }

    /* Attach super-block */
    sb->mount_parent = mount_parent;
    sb->root_dentry = root_ref;
    sb->mnt = mnt_ref;
    if (mount_parent) {
        sb->mount_next = mount_parent->mount_child;
        mount_parent->mount_child = sb;
    }
    ret = 0;
_exit:
    /* Unlock */
    mr_fs_nsp_unlock(mask);

    /* Put unused pins */
    if (ret != 0) {
        mr_dentry_put(root_ref);
        mr_dentry_put(mnt_ref);
    }
    return ret;
}

mr_err_t mr_super_block_detach(mr_super_block_t *sb, mr_dentry_t **mnt,
                               mr_dentry_t **root) {
    mr_super_block_t **link;
    int mask;

    /* Check parameter */
    MR_ASSERT((sb != MR_NULL) && MR_OBJECT_IS_INITED(sb));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(sb, superblock));
    MR_ASSERT((mnt != MR_NULL) && (root != MR_NULL));

    /* Lock */
    mask = mr_fs_nsp_lock();

    /* Detach must be prepared before the backing FS is notified */
    if (!sb->detaching) {
        /* Unlock */
        mr_fs_nsp_unlock(mask);
        return -MR_EINVAL;
    }
    if ((!sb->mnt) || (!sb->root_dentry)) {
        /* Unlock */
        mr_fs_nsp_unlock(mask);
        return -MR_ENOENT;
    }

    /* Hide the mounted root */
    if (sb->root_dentry != sb->mnt) {
        MR_ASSERT(sb->mnt->mounted == sb->root_dentry);
        sb->mnt->mounted = MR_NULL;
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

    /* Unlock */
    mr_fs_nsp_unlock(mask);
    return 0;
}

mr_super_block_t *mr_super_block_find_by_dentry(mr_dentry_t *dentry) {
    mr_super_block_t *sb;
    int mask;

    /* Check parameter */
    MR_ASSERT((dentry != MR_NULL) && MR_OBJECT_IS_INITED(dentry));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(dentry, dentry));

    /* Lock */
    mask = mr_fs_nsp_lock();

    /* Find the super-block mounted at or rooted at this dentry */
    for (sb = __fs_sb_list; sb; sb = sb->next) {
        if ((sb->mnt == dentry) || (sb->root_dentry == dentry)) {
            /* Get super-block */
            sb = mr_super_block_get(sb);

            /* Unlock */
            mr_fs_nsp_unlock(mask);
            return sb;
        }
    }

    /* Unlock */
    mr_fs_nsp_unlock(mask);
    return MR_NULL;
}

static mr_super_block_t *sb_obj_release(mr_object_t *obj) {
    mr_super_block_t *sb;

    /* Get super-block */
    sb = MR_CONTAINER_OF(obj, mr_super_block_t, parent);
    MR_ASSERT(sb->op_cnt == 0);
    MR_ASSERT((!sb->mount_parent) && (!sb->mount_child));
    MR_ASSERT((!sb->mnt) && (!sb->root_dentry));

    /* Detach from the global list if still linked. This is the safety net for
     * a super-block whose last reference is dropped without going through
     * mr_super_block_del(); remove is a no-op if already detached */
    super_block_remove(sb);

    /* Kill the super-block */
    super_block_kill(sb);
    return sb;
}

static void sb_dynamic_release(mr_object_t *obj) {
    mr_super_block_t *sb;

    /* Release super-block */
    sb = sb_obj_release(obj);

    /* Free super-block */
    mr_free(sb);
}
#endif /* defined(MR_USE_FS) */
