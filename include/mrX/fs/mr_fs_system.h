/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_FS_SYSTEM_H__
#define __MR_FS_SYSTEM_H__

#include <mrX/mr_config.h>
#if defined(MR_USE_FS)
#include <mrX/libc/mr_errno.h>
#endif /* defined(MR_USE_FS) */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup File-System
 * @{
 */

#if defined(MR_USE_FS)
struct mr_sblock;

/* File-system mount type */
typedef mr_err_t(mr_file_system_mount_t)(struct mr_sblock *sb,
                                         const char *source, int flags,
                                         void *data);
/* File-system unmount type */
typedef void(mr_file_system_unmount_t)(struct mr_sblock *sb);

/* File-system type */
typedef struct mr_file_system {
    const char *name;
    struct mr_file_system *next;
    mr_file_system_mount_t *mount;
    mr_file_system_unmount_t *unmount;
} mr_file_system_t;

/**
 * @brief This macro function initializes a static file-system.
 *
 * @param _name The file-system name.
 * @param _mount The mount callback.
 * @param _unmount The unmount callback(maybe MR_NULL).
 *
 * @note mount initializes the backend, sets the root inode and super-block
 * private data, and may access the device.
 * @note unmount releases backend memory resources after the final reference.
 * It must not access the device, assume the device is online, or fail.
 */
#define MR_FILE_SYSTEM_INIT(_name, _mount, _unmount)                           \
    {                                                                          \
        .name = (_name),                                                       \
        .mount = (_mount),                                                     \
        .unmount = (_unmount),                                                 \
        .next = MR_NULL,                                                       \
    }

/**
 * @brief This function registers a file-system.
 *
 * @param fs The file-system.
 * @return 0 on success, a negative error code on failure.
 *
 * @note The file-system descriptor must remain valid after registration.
 */
mr_err_t mr_file_system_register(mr_file_system_t *fs);

/**
 * @brief This function finds a registered file-system by name.
 *
 * @param name The file-system name.
 * @return The registered file-system on success, MR_NULL on failure.
 */
mr_file_system_t *mr_file_system_find(const char *name);
#endif /* defined(MR_USE_FS) */

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_FS_SYSTEM_H__ */
