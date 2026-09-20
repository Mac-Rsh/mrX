/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_FS_DENTRY_H__
#define __MR_FS_DENTRY_H__

#include <mrX/mr_config.h>
#if defined(MR_USE_FS)
#include <mrX/kernel/mr_object.h>
#include <mrX/kernel/mr_list.h>
#endif /* defined(MR_USE_FS) */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup FS-Dentry
 * @{
 */

#if defined(MR_USE_FS)
struct mr_dentry;
struct mr_inode;

/* Dentry state definition */
typedef enum mr_dentry_state {
    MR_DENTRY_STATE_ACTIVE,
    MR_DENTRY_STATE_MOVING,
    MR_DENTRY_STATE_UNLINKING,
    MR_DENTRY_STATE_MOUNTING,
    MR_DENTRY_STATE_RESERVED,
    MR_DENTRY_STATE_DEAD,
} mr_dentry_state_t;

/* Dentry name type */
typedef struct mr_dentry_name {
    const char *str;
    mr_uint32_t hash;
    mr_uint32_t len;
} mr_dentry_name_t;

/* Dentry operations type */
typedef struct mr_dentry_ops {
    mr_uint32_t (*hash)(const char *name, mr_uint32_t len);
    mr_bool_t (*compare)(const char *a, const char *b, mr_uint32_t len);
} mr_dentry_ops_t;

/* Dentry type */
MR_CLAZZES_EXPORT(dentry);
typedef struct mr_dentry {
    mr_object_t parent;
#if !defined(MR_CFG_FS_INLINE_NAME_LEN)
#define MR_CFG_FS_INLINE_NAME_LEN       (8U)
#endif /* defined(MR_CFG_FS_INLINE_NAME_LEN) */
    char iname[MR_CFG_FS_INLINE_NAME_LEN];
    mr_dentry_name_t name;
    struct mr_dentry *mounted;
    struct mr_dentry *upper;
    mr_list_t sibling, lower;
    const mr_dentry_ops_t *ops;
    struct mr_inode *inode;
    mr_uint8_t retained;
    mr_uint8_t state;
} mr_dentry_t;

/* Default hash seed for the djb2-xor hash used by dentry names */
#define MR_DENTRY_HASH_SEED             (5381U)

/**
 * @brief This macro function initializes a root dentry.
 *
 * @param _dentry The root dentry.
 *
 * @note This macro is only applicable to the FS root dentry(name "/").
 */
#define MR_DENTRY_ROOT_INIT(_dentry)                                           \
    {                                                                          \
        .parent = MR_OBJECT_INIT(MR_CLAZZES_STATIK(dentry)),                   \
        .iname = "/",                                                          \
        .name.str = (_dentry)->iname,                                          \
        .name.hash = MR_DENTRY_HASH_SEED,                                      \
        .name.len = 0,                                                         \
        .mounted = MR_NULL,                                                    \
        .upper = MR_NULL,                                                      \
        .sibling = MR_LIST_INIT(&(_dentry)->sibling),                          \
        .lower = MR_LIST_INIT(&(_dentry)->lower),                              \
        .ops = MR_NULL,                                                        \
        .inode = MR_NULL,                                                      \
        .state = MR_DENTRY_STATE_ACTIVE,                                       \
        .retained = MR_TRUE,                                                   \
    }

/**
 * @brief This function initializes a dentry.
 *
 * @param dentry The dentry.
 * @param ops The dentry operations(maybe MR_NULL for the default).
 * @param fmt The dentry name format.
 * @param ... The format arguments.
 * @return 0 on success, a negative error code on failure.
 *
 * @note Dentry names must not be empty, "." or "..".
 */
mr_err_t mr_dentry_init(mr_dentry_t *dentry, const mr_dentry_ops_t *ops,
                        const char *fmt, ...);

/**
 * @brief This function creates a dentry.
 *
 * @param ops The dentry operations(maybe MR_NULL for the default).
 * @param fmt The dentry name format.
 * @param ... The format arguments.
 * @return The dentry on success, MR_NULL on failure.
 *
 * @note Dentry names must not be empty, "." or "..".
 */
mr_dentry_t *mr_dentry_create(const mr_dentry_ops_t *ops, const char *fmt, ...);

/**
 * @brief This function deletes a dentry.
 *
 * @param dentry The dentry.
 * @return 0 on success, a negative error code on failure.
 *
 * @note Only a retained creation reference is dropped. References held by
 * lookup callers, handles or directory contexts must still be put separately.
 */
mr_err_t mr_dentry_del(mr_dentry_t *dentry);

/**
 * @brief This function adds a dentry to an upper dentry.
 *
 * @param dentry The dentry.
 * @param upper The upper dentry.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_dentry_add(mr_dentry_t *dentry, mr_dentry_t *upper);

/**
 * @brief This macro function checks if a dentry is added to a tree.
 */
#define MR_DENTRY_IS_ADDED(_dentry)     (!mr_list_is_empty(&(_dentry)->sibling))

/**
 * @brief This function removes a dentry from its upper dentry.
 *
 * @param dentry The dentry.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_dentry_remove(mr_dentry_t *dentry);

/**
 * @brief This function moves a dentry to a new parent and/or renames it.
 *
 * @param dentry The dentry to move(must already be added to a tree).
 * @param upper The new upper(parent) dentry.
 * @param name The new single-component name(no '/').
 * @return 0 on success, a negative error code on failure.
 *
 * @note The dentry must not be moved into itself or its own
 * subtree(-MR_EINVAL).
 * @note A name already in use under the new parent is rejected(-MR_EEXIST).
 * @note Moves across super-block instances are rejected(-MR_EXDEV), even
 * when both instances use the same file-system type.
 * @note A prepared move serializes other moves in its super-block until
 * commit or cancellation; competing moves return -MR_EBUSY without waiting.
 * Trees without a super-block share a separate move guard and can not move
 * entries into or out of a tree backed by a super-block.
 * @note This function does not rename the backing filesystem node.
 */
mr_err_t mr_dentry_move(mr_dentry_t *dentry, mr_dentry_t *upper,
                        const char *name);

/**
 * @brief This function renames a filesystem dentry.
 *
 * @param dentry The dentry to rename(must already be added to a tree).
 * @param upper The new upper(parent) dentry.
 * @param name The new single-component name(no '/').
 * @return 0 on success, a negative error code on failure.
 *
 * @note Source and destination must belong to the same super-block instance;
 * crossing this boundary returns -MR_EXDEV before the backend callback.
 * @note Competing moves in the same super-block and a source being mounted
 * return -MR_EBUSY. Different super-blocks can run rename callbacks in parallel.
 */
mr_err_t mr_dentry_rename(mr_dentry_t *dentry, mr_dentry_t *upper,
                          const char *name);

/**
 * @brief This function unlinks a filesystem dentry.
 *
 * @param dentry The
 * dentry to unlink(must already be added to a tree).
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_dentry_unlink(mr_dentry_t *dentry);

/**
 * @brief This function gets the n-th child of a dentry.
 *
 * @param parent The parent dentry.
 * @param index The child index(0-based).
 * @return The child on success, MR_NULL if the index is out of range. The
 * returned dentry holds one reference; call mr_dentry_put() when done.
 */
mr_dentry_t *mr_dentry_child_get(mr_dentry_t *parent, mr_size_t index);

/**
 * @brief This function checks if a dentry has no children.
 *
 * @param dentry The dentry.
 * @return MR_TRUE if the dentry has no children, MR_FALSE otherwise.
 */
mr_bool_t mr_dentry_is_empty(mr_dentry_t *dentry);

/**
 * @brief This function looks up a dentry from a path.
 *
 * @param start The start dentry.
 * @param limit The lookup limit(dentry that '..' cannot escape).
 * @param path The relative path.
 * @param result The resolved dentry.
 * @return 0 on success, a negative error code on failure.
 *
 * @note The returned dentry holds one reference; call mr_dentry_put() to
 * release it when no longer needed.
 * @note Paths are relative to start.
 */
mr_err_t mr_dentry_lookup(mr_dentry_t *start, mr_dentry_t *limit,
                          const char *path, mr_dentry_t **result);

/**
 * @brief This function binds an inode to a dentry.
 *
 * @param dentry The dentry(must not have an inode bound).
 * @param inode The inode(the dentry acquires its own reference; the caller's
 * reference is unaffected).
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_dentry_instantiate(mr_dentry_t *dentry, struct mr_inode *inode);

/**
 * @brief This function unbinds an inode from a dentry.
 *
 * @param dentry The dentry(must have an inode bound).
 * @return 0 on success, a negative error code on failure.
 *
 * @note This function releases the inode reference owned by the dentry. Other
 * inode references are unaffected.
 */
mr_err_t mr_dentry_uninstantiate(mr_dentry_t *dentry);

/**
 * @brief This function checks whether a dentry is the global file-system
 * root.
 *
 * @param dentry The dentry to check.
 * @return MR_TRUE on root, otherwise MR_FALSE.
 */
mr_bool_t mr_dentry_is_root(mr_dentry_t *dentry);

/**
 * @brief This macro function gets a dentry name.
 */
#define MR_DENTRY_NAME(_dentry) ((_dentry)->name.str)

/**
 * @brief This macro function gets a dentry name length(excluding '\0').
 */
#define MR_DENTRY_NAME_LEN(_dentry) ((_dentry)->name.len)

/**
 * @brief This function gets a dentry.
 *
 * @param dentry The dentry.
 * @return The dentry on success, MR_NULL on failure.
 */
MR_INLINE mr_dentry_t *mr_dentry_get(mr_dentry_t *dentry) {
    /* Get dentry reference */
    return (mr_dentry_t *)mr_object_get((mr_object_t *)dentry);
}

/**
 * @brief This function puts a dentry.
 *
 * @param dentry The dentry.
 * @return MR_TRUE on end of life cycle, MR_FALSE otherwise.
 */
MR_INLINE mr_bool_t mr_dentry_put(mr_dentry_t *dentry) {
    /* Put dentry reference */
    return mr_object_put((mr_object_t *)dentry);
}
#endif /* defined(MR_USE_FS) */

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_FS_DENTRY_H__ */
