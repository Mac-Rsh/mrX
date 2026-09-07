/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_FS_SBLOCK_H__
#define __MR_FS_SBLOCK_H__

#include <mrX/mr_config.h>
#if defined(MR_USE_FS)
#include <mrX/fs/mr_fs_system.h>
#include <mrX/fs/mr_fs_dentry.h>
#include <mrX/fs/mr_fs_inode.h>
#endif /* defined(MR_USE_FS) */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup Super-block
 * @{
 */

#if defined(MR_USE_FS)
/* Super-block state definition */
typedef enum mr_sblock_state {
    MR_SBLOCK_STATE_INACTIVE,
    MR_SBLOCK_STATE_ACTIVE,
    MR_SBLOCK_STATE_OFFLINE,
} mr_sblock_state_t;

/* Super-block type */
MR_CLAZZES_EXPORT(sblock);
typedef struct mr_sblock {
    mr_object_t parent;
    mr_dentry_t *mnt;
    mr_dentry_t *root_dentry;
    mr_inode_t *root;
    struct mr_sblock *next;
    struct mr_sblock *mount_parent;
    struct mr_sblock *mount_child;
    struct mr_sblock *mount_next;
    struct mr_file_system *type;
    mr_uint8_t move_busy;
    mr_uint8_t state;
    mr_size_t op_cnt;
    mr_size_t open_cnt;
    int flags;
    const mr_dentry_ops_t *dops;
    void *priv;
} mr_sblock_t;

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
mr_err_t mr_sblock_create(mr_file_system_t *type, const char *source,
                               int flags, void *data, mr_sblock_t **out);

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
mr_err_t mr_sblock_del(mr_sblock_t *sb);

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
mr_err_t mr_sblock_attach(mr_sblock_t *sb, mr_dentry_t *mnt,
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
 * @note A super-block containing in-flight operations or a prepared move
 * returns -MR_EBUSY.
 * @note ACTIVE returns -MR_EBUSY while files are open. OFFLINE ignores open
 * files because media removal cannot be rejected.
 * @note ACTIVE becomes INACTIVE. OFFLINE stays OFFLINE.
 */
mr_err_t mr_sblock_detach(mr_sblock_t *sb, mr_dentry_t **mnt,
                               mr_dentry_t **root);

/**
 * @brief This function enters a super-block operation.
 *
 * @param sb The super-block.
 * @return 0 on success, a negative error code on failure.
 *
 * @note ACTIVE increments op_cnt. OFFLINE returns -MR_ENODEV. INACTIVE
 * returns -MR_ENOENT.
 */
mr_err_t mr_sblock_op_enter(mr_sblock_t *sb);

/**
 * @brief This function leaves a super-block operation.
 *
 * @param sb The super-block.
 */
void mr_sblock_op_exit(mr_sblock_t *sb);

/**
 * @brief This function marks a mount subtree permanently offline.
 *
 * @param sb The root of the subtree.
 *
 * @note Every reachable child is marked OFFLINE and hidden from the
 * namespace. The topology links remain until each super-block is detached.
 */
void mr_sblock_offline_subtree(mr_sblock_t *sb);

/**
 * @brief This function gets the first leaf of a mount subtree.
 *
 * @param sb The root of the subtree.
 * @return The leaf super-block with one reference, or MR_NULL on failure.
 *
 * @note Call mr_sblock_put() when the leaf is no longer needed.
 */
mr_sblock_t *mr_sblock_subtree_leaf_get(mr_sblock_t *sb);

/**
 * @brief This function acquires an open-file lifetime on a super-block.
 *
 * @param sb The super-block.
 * @return 0 on success, a negative error code on failure.
 *
 * @note Only ACTIVE accepts new open files. Each successful acquisition must
 * be paired with mr_sblock_file_release().
 */
mr_err_t mr_sblock_file_acquire(mr_sblock_t *sb);

/**
 * @brief This function releases an open-file lifetime.
 *
 * @param sb The super-block.
 */
void mr_sblock_file_release(mr_sblock_t *sb);

/**
 * @brief This function finds a mounted super-block by dentry.
 *
 * @param dentry The mount-point or mounted root dentry.
 * @return The super-block on success, MR_NULL on failure.
 *
 * @note The returned super-block holds one reference. Call
 * mr_sblock_put() when it is no longer needed.
 */
mr_sblock_t *mr_sblock_find_by_dentry(mr_dentry_t *dentry);

/**
 * @brief This function gets a super-block.
 *
 * @param sb The super-block.
 * @return The super-block on success, MR_NULL on failure.
 */
MR_INLINE mr_sblock_t *mr_sblock_get(mr_sblock_t *sb) {
    /* Get super-block reference */
    return (mr_sblock_t *)mr_object_get((mr_object_t *)sb);
}

/**
 * @brief This function puts a super-block.
 *
 * @param sb The super-block.
 * @return MR_TRUE on end of life cycle, MR_FALSE otherwise.
 */
MR_INLINE mr_bool_t mr_sblock_put(mr_sblock_t *sb) {
    /* Put super-block reference */
    return mr_object_put((mr_object_t *)sb);
}
#endif /* defined(MR_USE_FS) */

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_FS_SBLOCK_H__ */
