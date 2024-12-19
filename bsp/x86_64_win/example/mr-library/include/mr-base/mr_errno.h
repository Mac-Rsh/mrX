/**
 * @copyright (c) 2024, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_ERRNO_H__
#define __MR_ERRNO_H__

#include <mr-base/mr_types.h>
#include <mr_config.h>
#ifdef MR_USE_LIBC
#include <errno.h>
#endif /* MR_USE_LIBC */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup Errno
 * @{
 */

/* Errno definition */
#ifdef MR_USE_LIBC
#define MR_EPERM                        EPERM
#define MR_ENOENT                       ENOENT
#define MR_EIO                          EIO
#define MR_ENOMEM                       ENOMEM
#define MR_EBUSY                        EBUSY
#define MR_EEXIST                       EEXIST
#define MR_EINVAL                       EINVAL
#define MR_ETIMEDOUT                    ETIMEDOUT
#else
#define MR_EPERM                        1
#define MR_ENOENT                       2
#define MR_EIO                          5
#define MR_ENOMEM                       12
#define MR_EBUSY                        16
#define MR_EEXIST                       17
#define MR_EINVAL                       22
#define MR_ETIMEDOUT                    138
#endif /* MR_USE_LIBC */
#define MR_EOK                          0

/**
 * @brief This function gets the error string.
 *
 * @param errnum The error number.
 * @return The error string.
 */
const char *mr_strerror(int errnum);

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_ERRNO_H__ */
