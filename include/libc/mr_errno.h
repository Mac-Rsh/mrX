/**
 * @copyright (c) 2024, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_ERRNO_H__
#define __MR_ERRNO_H__

#include <mr_config.h>
#if defined(MR_USE_LIBC_ERRNO)
#include <errno.h>
#elif defined(MR_USE_3PARTY_ERRNO)
#include <3party/libc/mr_errno.h>
#endif /* defined(MR_USE_LIBC_ERRNO) */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup Errno
 * @{
 */

/* Error type */
typedef int                             mr_err_t;

/* Errno definition */
#if defined(MR_USE_LIBC_ERRNO)
#define MR_EPERM                        EPERM
#define MR_ENOENT                       ENOENT
#define MR_EIO                          EIO
#define MR_EAGAIN                       EAGAIN
#define MR_ENOMEM                       ENOMEM
#define MR_EBUSY                        EBUSY
#define MR_EEXIST                       EEXIST
#define MR_EINVAL                       EINVAL
#define MR_ETIMEDOUT                    ETIMEDOUT
#elif defined(MR_USE_3PARTY_ERRNO)
/* In '3party/libc/mr_errno.h' */
#else
#define MR_EPERM                        1
#define MR_ENOENT                       2
#define MR_EIO                          5
#define MR_EAGAIN                       11
#define MR_ENOMEM                       12
#define MR_EBUSY                        16
#define MR_EEXIST                       17
#define MR_EINVAL                       22
#define MR_ETIMEDOUT                    116
#endif /* defined(MR_USE_LIBC_ERRNO) */

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_ERRNO_H__ */
