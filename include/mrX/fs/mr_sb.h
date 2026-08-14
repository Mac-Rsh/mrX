/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_FS_SB_H__
#define __MR_FS_SB_H__

#include <mrX/mr_config.h>
#if defined(MR_USE_FS)
#include <mrX/fs/mr_dentry.h>
#include <mrX/fs/mr_inode.h>
#include <mrX/fs/mr_fs.h>
#endif /* defined(MR_USE_FS) */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup Super-block
 * @{
 */

#if defined(MR_USE_FS)
/* Super-block type */
MR_CLAZZES_EXPORT(superblock);
typedef struct mr_super_block {
    mr_object_t parent;
    mr_dentry_t *mnt;
    mr_dentry_t *root_dentry;
    mr_inode_t *root;
    int flags;
    struct mr_super_block *next;
    struct mr_super_block *mount_parent;
    struct mr_super_block *mount_child;
    struct mr_super_block *mount_next;
    struct mr_file_system *type;
    const mr_dentry_ops_t *dops;
    mr_bool_t detaching;
    mr_size_t op_cnt;
    void *priv;
} mr_super_block_t;

/**
 * @brief This function creates a dynamic super-block and mounts the
 * file-system.
 *
 * @param type The file-system.
 * @param source The mount source(maybe MR_NULL).
 * @param flags The mount flags.
 * @param data The mount private data.
 * @param out The created super-block.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_super_block_create(mr_file_system_t *type, const char *source,
                               int flags, void *data, mr_super_block_t **out);

/**
 * @brief This function prepares a super-block for detach.
 *
 * @param sb The super-block.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_super_block_detach_prepare(mr_super_block_t *sb);

/**
 * @brief This function cancels a prepared super-block detach.
 *
 * @param sb The super-block.
 */
void mr_super_block_detach_cancel(mr_super_block_t *sb);

/**
 * @brief This function enters a super-block namespace operation.
 *
 * @param sb The super-block.
 * @return 0 on success, or -MR_EBUSY while the super-block is detaching.
 */
mr_err_t mr_super_block_op_enter(mr_super_block_t *sb);

/**
 * @brief This function leaves a super-block namespace operation.
 *
 * @param sb The super-block.
 */
void mr_super_block_op_exit(mr_super_block_t *sb);

/**
 * @brief This function removes and deletes a super-block.
 *
 * @param sb The super-block.
 * @return 0 on success, a negative error code on failure.
 *
 * @note This function removes the super-block from the global list and
 * releases its root inode creation reference. Final file-system cleanup is
 * deferred until the final super-block reference is released.
 */
mr_err_t mr_super_block_del(mr_super_block_t *sb);

/**
 * @brief This function attaches a super-block to a mount-point.
 *
 * @param sb The super-block.
 * @param mnt The mount-point dentry.
 * @param root The mounted root dentry(bound to the super-block root inode).
 * @return 0 on success, a negative error code on failure.
 *
 * @note On success, the super-block acquires references to mnt and root and
 * links itself into the mount topology.
 */
mr_err_t mr_super_block_attach(mr_super_block_t *sb, mr_dentry_t *mnt,
                               mr_dentry_t *root);

/**
 * @brief This function detaches a super-block from its mount-point.
 *
 * @param sb The super-block.
 * @param mnt The detached mount-point dentry.
 * @param root The detached mounted root dentry.
 * @return 0 on success, a negative error code on failure.
 *
 * @note The references previously held by the super-block are transferred to
 * mnt and root on success. The caller must release both references.
 * @note A super-block containing child mounts returns -MR_EBUSY.
 */
mr_err_t mr_super_block_detach(mr_super_block_t *sb, mr_dentry_t **mnt,
                               mr_dentry_t **root);

/**
 * @brief This function finds a mounted super-block by dentry.
 *
 * @param dentry The mount-point or mounted root dentry.
 * @return The super-block on success, MR_NULL on failure.
 *
 * @note The returned super-block holds one reference. Call
 * mr_super_block_put() when it is no longer needed.
 */
mr_super_block_t *mr_super_block_find_by_dentry(mr_dentry_t *dentry);

/**
 * @brief This function gets a super-block.
 *
 * @param sb The super-block.
 * @return The super-block on success, MR_NULL on failure.
 */
MR_INLINE mr_super_block_t *mr_super_block_get(mr_super_block_t *sb) {
    /* Get super-block reference */
    return (mr_super_block_t *)mr_object_get((mr_object_t *)sb);
}

/**
 * @brief This function puts a super-block.
 *
 * @param sb The super-block.
 * @return MR_TRUE on end of life cycle, MR_FALSE otherwise.
 */
MR_INLINE mr_bool_t mr_super_block_put(mr_super_block_t *sb) {
    /* Put super-block reference */
    return mr_object_put((mr_object_t *)sb);
}
#endif /* defined(MR_USE_FS) */

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_FS_SB_H__ */
