/**
 * @copyright (c) 2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2026-08-13        MacRsh        First version
 */

#include <mrX/fs/mr_nsp.h>
#if defined(MR_USE_FS)
#include <mrX/kernel/mr_spinlock.h>
#include <mrX/fs/mr_dentry.h>

/* Global FS root dentry("/") */
static mr_dentry_t __fs_root = MR_DENTRY_ROOT_INIT(&__fs_root);
/* FS namespace lock */
static mr_spinlock_t __fs_nsp_lock = MR_SPINLOCK_INIT();
#if !defined(MR_USE_FS_MULTIUSER)
/* Single user FS namespace */
static mr_fs_nsp_t __fs_nsp;
#endif /* !defined(MR_USE_FS_MULTIUSER) */

mr_err_t mr_fs_nsp_init(mr_fs_nsp_t *nsp) {
    MR_ASSERT(nsp != MR_NULL);

    /* Init FS namespace */
    nsp->root = mr_dentry_get(&__fs_root);
    if (!nsp->root) {
        nsp->cwd = MR_NULL;
        return -MR_EINVAL;
    }
    nsp->cwd = mr_dentry_get(&__fs_root);
    if (!nsp->cwd) {
        mr_dentry_put(nsp->root);
        nsp->root = MR_NULL;
        return -MR_EINVAL;
    }
    return 0;
}

#if !defined(MR_USE_FS_MULTIUSER)
mr_fs_nsp_t *mr_fs_nsp_current(void) {
    /* Return single user FS namespace */
    return &__fs_nsp;
}
#endif /* !defined(MR_USE_FS_MULTIUSER) */

mr_bool_t mr_fs_nsp_is_root(const struct mr_dentry *dentry) {
    /* Check global FS root */
    return (dentry == &__fs_root) ? MR_TRUE : MR_FALSE;
}

int mr_fs_nsp_lock(void) {
    /* Lock */
    return mr_spinlock_lock_irqsave(&__fs_nsp_lock);
}

void mr_fs_nsp_unlock(int mask) {
    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__fs_nsp_lock, mask);
}
#endif /* defined(MR_USE_FS) */
