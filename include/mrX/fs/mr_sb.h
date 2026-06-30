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
    mr_inode_t *root;
    int flags;
    struct mr_super_block *next;
    struct mr_file_system *type;
    const mr_dentry_ops_t *dops;
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
 * @return The super-block on success, MR_NULL on failure.
 */
mr_super_block_t *mr_super_block_create(mr_file_system_t *type,
                                        const char *source, int flags,
                                        void *data);

/**
 * @brief This function deletes a super-block(drops one reference).
 *
 * @param sb The super-block.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_super_block_del(mr_super_block_t *sb);

/**
 * @brief This function finds a super-block by its mount-point dentry.
 *
 * @param mnt The mount-point dentry.
 * @return The super-block on success, MR_NULL on failure. The returned object
 * holds one reference; call mr_super_block_put() when done.
 */
mr_super_block_t *mr_super_block_find_by_mnt(mr_dentry_t *mnt);

/**
 * @brief This function detaches the mount-point dentry from a super-block.
 *
 * @param sb The super-block.
 */
void mr_super_block_detach_mnt(mr_super_block_t *sb);

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
