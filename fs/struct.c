/**
 * @copyright (c) 2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/fs/mr_fs_struct.h>
#if defined(MR_USE_FS)
#include <mrX/kernel/mr_spinlock.h>
#include <mrX/fs/mr_fs_dentry.h>

/* Global FS root dentry("/") */
static mr_dentry_t __fs_root = MR_DENTRY_ROOT_INIT(&__fs_root);
/* FS structure lock */
static mr_spinlock_t __fs_struct_lock = MR_SPINLOCK_INIT();
#if !defined(MR_USE_FS_MULTIUSER)
/* Single user FS structure */
static mr_fs_struct_t __fs_struct;
#endif /* !defined(MR_USE_FS_MULTIUSER) */

struct mr_dentry *mr_fs_struct_root(void) {
    /* Return root */
    return (struct mr_dentry *)&__fs_root;
}

mr_err_t mr_fs_struct_init(mr_fs_struct_t *fs_struct) {
    MR_ASSERT(fs_struct != MR_NULL);

    /* Init FS structure */
    fs_struct->root = mr_dentry_get(&__fs_root);
    if (!fs_struct->root) {
        fs_struct->cwd = MR_NULL;
        return -MR_EINVAL;
    }
    fs_struct->cwd = mr_dentry_get(&__fs_root);
    if (!fs_struct->cwd) {
        mr_dentry_put(fs_struct->root);
        fs_struct->root = MR_NULL;
        return -MR_EINVAL;
    }
    return 0;
}

#if !defined(MR_USE_FS_MULTIUSER)
mr_fs_struct_t *mr_fs_struct_current(void) {
    /* Return single user FS structure */
    return &__fs_struct;
}
#endif /* !defined(MR_USE_FS_MULTIUSER) */

int mr_fs_struct_lock(void) {
    /* Lock */
    return mr_spinlock_lock_irqsave(&__fs_struct_lock);
}

void mr_fs_struct_unlock(int mask) {
    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__fs_struct_lock, mask);
}
#endif /* defined(MR_USE_FS) */
