/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_FS_FS_H__
#define __MR_FS_FS_H__

#include <mrX/mr_config.h>
#if defined(MR_USE_FS)
#include <mrX/kernel/mr_object.h>
#endif /* defined(MR_USE_FS) */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup File-System
 * @{
 */

#if defined(MR_USE_FS)
struct mr_super_block;

/* File-system mount type */
typedef mr_err_t(mr_file_system_mount_t)(struct mr_super_block *sb,
                                         const char *source, int flags,
                                         void *data);
/**
 * @brief File-system super-block final-release callback type.
 *
 * @param sb The super-block.
 *
 * @note The VFS releases and clears sb->root before invoking this callback.
 * The callback must release only file-system-private resources.
 * @note After a successful mount, this callback runs when the final
 * super-block reference is released. It also runs when later VFS mount
 * initialization fails.
 */
typedef void(mr_file_system_kill_sb_t)(struct mr_super_block *sb);

/* File-system type */
MR_CLAZZES_EXPORT(file_system);
typedef struct mr_file_system {
    mr_object_t parent;
    const char *name;
    struct mr_file_system *next;
    mr_file_system_mount_t *mount;
    mr_file_system_kill_sb_t *kill_sb;
} mr_file_system_t;

/**
 * @brief This macro function initializes a static file-system.
 *
 * @param _name The file-system name.
 * @param _mount The mount callback.
 * @param _kill_sb The super-block destroy callback.
 */
#define MR_FILE_SYSTEM_INIT(_name, _mount, _kill_sb)                           \
    {                                                                          \
        .parent = MR_OBJECT_INIT(MR_CLAZZES_STATIK(file_system)),              \
        .name = (_name),                                                       \
        .mount = (_mount),                                                     \
        .kill_sb = (_kill_sb),                                                 \
        .next = MR_NULL,                                                       \
    }

/**
 * @brief This function initializes a static file-system.
 *
 * @param fs The file-system.
 * @param name The file-system name.
 * @param mount The mount callback.
 * @param kill_sb The super-block destroy callback.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_file_system_init(mr_file_system_t *fs, const char *name,
                             mr_file_system_mount_t *mount,
                             mr_file_system_kill_sb_t *kill_sb);

/**
 * @brief This function deletes a file-system object.
 *
 * @param fs The file-system.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_file_system_del(mr_file_system_t *fs);

/**
 * @brief This function registers a file-system.
 *
 * @param fs The file-system.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_file_system_register(mr_file_system_t *fs);

/**
 * @brief This function unregisters a file-system.
 *
 * @param fs The file-system.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_file_system_unregister(mr_file_system_t *fs);

/**
 * @brief This function finds a registered file-system by name.
 *
 * @param name The file-system name.
 * @return The file-system on success, MR_NULL on failure. The returned
 * object holds one reference; call mr_file_system_put() when done.
 */
mr_file_system_t *mr_file_system_find(const char *name);

/**
 * @brief This function gets a file-system object.
 *
 * @param fs The file-system.
 * @return The file-system on success, MR_NULL on failure.
 */
MR_INLINE mr_file_system_t *mr_file_system_get(mr_file_system_t *fs) {
    /* Get file-system reference */
    return (mr_file_system_t *)mr_object_get((mr_object_t *)fs);
}

/**
 * @brief This function puts a file-system object.
 *
 * @param fs The file-system.
 * @return MR_TRUE on end of life cycle, MR_FALSE otherwise.
 */
MR_INLINE mr_bool_t mr_file_system_put(mr_file_system_t *fs) {
    /* Put file-system reference */
    return mr_object_put((mr_object_t *)fs);
}
#endif /* defined(MR_USE_FS) */

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_FS_FS_H__ */
