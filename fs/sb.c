/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/fs/mr_sb.h>
#if defined(MR_USE_FS)
#include <mrX/kernel/mr_spinlock.h>
#include <mrX/libc/mr_malloc.h>

/* Super-block list lock */
static mr_spinlock_t __sb_lock = MR_SPINLOCK_INIT();
/* Super-block list */
static mr_super_block_t *__sb_list = MR_NULL;

/* Super-block clazzes(dynamic) */
static void sb_dynamic_release(mr_object_t *obj);
MR_CLAZZES_DEFINE(superblock, MR_NULL, sb_dynamic_release, MR_NULL);

static void sb_list_add(mr_super_block_t *sb) {
    int mask;

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__sb_lock);

    /* Add super-block to the list */
    sb->next = __sb_list;
    __sb_list = sb;

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__sb_lock, mask);
}

static mr_err_t super_block_init(mr_super_block_t *sb, const mr_clazz_t *clazz,
                                 mr_file_system_t *type, const char *source,
                                 int flags, void *data) {
    mr_err_t ret;

    /* Get file-system */
    sb->type = mr_file_system_get(type);
    if (!sb->type) {
        return -MR_ENOENT;
    }
    if ((!sb->type->mount) || (!sb->type->kill_sb)) {
        ret = -MR_EIO;
        goto _exit;
    }

    /* Init super-block */
    sb->mnt = MR_NULL;
    sb->root = MR_NULL;
    sb->flags = flags;
    sb->dops = MR_NULL;
    sb->priv = MR_NULL;
    sb->next = MR_NULL;

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
    /* Kill the super-block */
    sb->type->kill_sb(sb);
_exit:
    /* Put file-system */
    mr_file_system_put(sb->type);
    sb->type = MR_NULL;
    return ret;
}

mr_super_block_t *mr_super_block_create(mr_file_system_t *type,
                                        const char *source, int flags,
                                        void *data) {
    const mr_clazz_t *clazz = MR_CLAZZES_DYNAMIC(superblock);
    mr_super_block_t *sb;
    mr_err_t ret;

    /* Check parameter */
    MR_ASSERT((type != MR_NULL) && MR_OBJECT_IS_INITED(type));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(type, file_system));

    /* Allocate super-block */
    sb = mr_malloc(sizeof(mr_super_block_t));
    if (!sb) {
        return MR_NULL;
    }

    /* Init super-block */
    ret = super_block_init(sb, clazz, type, source, flags, data);
    if (ret != 0) {
        mr_free(sb);
        return MR_NULL;
    }
    return sb;
}

static mr_err_t super_block_remove(mr_super_block_t *sb) {
    mr_super_block_t **link;
    int mask;

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__sb_lock);

    /* Find the added entry by address. A never-added object may have an
     * arbitrary next pointer, so do not trust sb->next here */
    for (link = &__sb_list; *link; link = &(*link)->next) {
        if (*link == sb) {
            break;
        }
    }

    /* Not registered */
    if (!(*link)) {
        /* Unlock */
        mr_spinlock_unlock_irqrestore(&__sb_lock, mask);
        return -MR_ENOENT;
    }

    /* Remove from the list */
    *link = sb->next;
    sb->next = MR_NULL;

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__sb_lock, mask);
    return 0;
}

static void super_block_kill(mr_super_block_t *sb) {
    if (sb->type) {
        /* Kill the super-block */
        if (sb->type->kill_sb) {
            sb->type->kill_sb(sb);
        }

        /* Put file-system */
        mr_file_system_put(sb->type);
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

    /* Kill the super-block */
    super_block_kill(sb);

    /* Delete object */
    mr_object_del((mr_object_t *)sb);
    return 0;
}

mr_super_block_t *mr_super_block_find_by_mnt(mr_dentry_t *mnt) {
    mr_super_block_t *sb;
    int mask;

    /* Check parameter */
    MR_ASSERT((mnt != MR_NULL) && MR_OBJECT_IS_INITED(mnt));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(mnt, dentry));

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__sb_lock);

    /* Find the super-block mounted at this dentry */
    for (sb = __sb_list; sb; sb = sb->next) {
        if (sb->mnt == mnt) {
            /* Get super-block */
            sb = mr_super_block_get(sb);

            /* Unlock */
            mr_spinlock_unlock_irqrestore(&__sb_lock, mask);
            return sb;
        }
    }

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__sb_lock, mask);
    return MR_NULL;
}

void mr_super_block_detach_mnt(mr_super_block_t *sb) {
    int mask;

    /* Check parameter */
    MR_ASSERT((sb != MR_NULL) && MR_OBJECT_IS_INITED(sb));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(sb, superblock));

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__sb_lock);

    /* Detach mount-point dentry */
    sb->mnt = MR_NULL;

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__sb_lock, mask);
}

static mr_super_block_t *sb_obj_release(mr_object_t *obj) {
    mr_super_block_t *sb;

    /* Get super-block */
    sb = MR_CONTAINER_OF(obj, mr_super_block_t, parent);

    /* Detach from the global list if still linked. This is the safety net for
     * a super-block whose last reference is dropped without going through
     * mr_super_block_del(); remove is a no-op if already detached */
    (void)super_block_remove(sb);

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
