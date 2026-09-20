/**
 * @copyright (c) 2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_FS_STRUCT_H__
#define __MR_FS_STRUCT_H__

#include <mrX/mr_config.h>
#if defined(MR_USE_FS)
#include <mrX/libc/mr_types.h>
#include <mrX/libc/mr_errno.h>
#endif /* defined(MR_USE_FS) */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup FS-Struct
 * @{
 */

#if defined(MR_USE_FS)
struct mr_dentry;

/* File-system structure type */
typedef struct mr_fs_struct {
    struct mr_dentry *root;
    struct mr_dentry *cwd;
} mr_fs_struct_t;

/**
 * @brief This function gets the global file-system root dentry.
 *
 * @return The global file-system root dentry.
 */
struct mr_dentry *mr_fs_struct_root(void);

/**
 * @brief This function initializes a file-system structure.
 *
 * @param fs_struct The file-system structure.
 * @return 0 on success, a negative error code on failure.
 *
 * @note The initialized root and current working directory both refer to the
 * global FS root.
 */
mr_err_t mr_fs_struct_init(mr_fs_struct_t *fs_struct);

/**
 * @brief This function gets the current file-system structure.
 *
 * @return The current file-system structure, or MR_NULL if unavailable.
 *
 * @note When MR_USE_FS_MULTIUSER is enabled, the platform must provide this
 * function. Otherwise, the FS provides a single global structure.
 */
mr_fs_struct_t *mr_fs_struct_current(void);

/**
 * @brief This function locks the file-system structure and disables
 * interrupts.
 *
 * @return The previous interrupt mask.
 *
 * @note The returned mask must be passed to mr_fs_struct_unlock(). The locked
 * section must not block or call file-system implementation callbacks.
 */
int mr_fs_struct_lock(void);

/**
 * @brief This function unlocks the file-system structure and restores
 * interrupts.
 *
 * @param mask The interrupt mask returned by the matching
 * mr_fs_struct_lock().
 */
void mr_fs_struct_unlock(int mask);
#endif /* defined(MR_USE_FS) */

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_FS_STRUCT_H__ */
