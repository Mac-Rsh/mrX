/**
 * @copyright (c) 2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2026-08-13        MacRsh        First version
 */

#ifndef __MR_FS_NSP_H__
#define __MR_FS_NSP_H__

#include <mrX/mr_config.h>
#if defined(MR_USE_FS)
#include <mrX/libc/mr_types.h>
#include <mrX/libc/mr_errno.h>
#endif /* defined(MR_USE_FS) */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#if defined(MR_USE_FS)
struct mr_dentry;

/* File-system namespace type */
typedef struct mr_fs_nsp {
    struct mr_dentry *root;
    struct mr_dentry *cwd;
} mr_fs_nsp_t;

/**
 * @brief This function initializes a file-system namespace.
 *
 * @param nsp The file-system namespace.
 * @return 0 on success, a negative error code on failure.
 *
 * @note The initialized ROOT and current working directory both refer to the
 * global FS ROOT.
 */
mr_err_t mr_fs_nsp_init(mr_fs_nsp_t *nsp);

/**
 * @brief This function gets the current file-system namespace.
 *
 * @return The current file-system namespace, or MR_NULL if unavailable.
 *
 * @note When MR_USE_FS_MULTIUSER is enabled, the platform must provide this
 * function. Otherwise, the FS provides a single global namespace.
 */
mr_fs_nsp_t *mr_fs_nsp_current(void);

/**
 * @brief This function checks whether a dentry is the global file-system
 * ROOT.
 *
 * @param dentry The dentry to check.
 * @return MR_TRUE on ROOT, otherwise MR_FALSE.
 */
mr_bool_t mr_fs_nsp_is_root(const struct mr_dentry *dentry);

/**
 * @brief This function locks the file-system namespace and disables
 * interrupts.
 *
 * @return The previous interrupt mask.
 *
 * @note The returned mask must be passed to mr_fs_nsp_unlock(). The locked
 * section must not block or call file-system implementation callbacks.
 */
int mr_fs_nsp_lock(void);

/**
 * @brief This function unlocks the file-system namespace and restores
 * interrupts.
 *
 * @param mask The interrupt mask returned by the matching
 * mr_fs_nsp_lock().
 */
void mr_fs_nsp_unlock(int mask);
#endif /* defined(MR_USE_FS) */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_FS_NSP_H__ */
