/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_LIBC_ERRNO_H__
#define __MR_LIBC_ERRNO_H__

#include <mrX/mr_config.h>
#if defined(MR_USE_ERRNO_STD)
#include <errno.h>
#elif defined(MR_USE_ERRNO_3RD)
#include <mrX/platform/libc/mr_errno.h>
#endif /* defined(MR_USE_ERRNO_STD) */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup Errno
 * @{
 */

/* Error type */
typedef int mr_err_t;

/* Errno definition */
#if defined(MR_USE_ERRNO_STD)
#define MR_EPERM                        EPERM
#define MR_ENOENT                       ENOENT
#define MR_EIO                          EIO
#define MR_ENODEV                       ENODEV
#define MR_EAGAIN                       EAGAIN
#define MR_ENOMEM                       ENOMEM
#define MR_EBUSY                        EBUSY
#define MR_EEXIST                       EEXIST
#define MR_EINVAL                       EINVAL
#define MR_ENOSYS                       ENOSYS
#define MR_ETIMEDOUT                    ETIMEDOUT
#define MR_ECANCELED                    ECANCELED
#define MR_EBADF                        EBADF
#define MR_ENOTDIR                      ENOTDIR
#define MR_EISDIR                       EISDIR
#define MR_ENOTEMPTY                    ENOTEMPTY
#define MR_EMFILE                       EMFILE
#define MR_ENAMETOOLONG                 ENAMETOOLONG
#define MR_EXDEV                        EXDEV
#define MR_ERANGE                       ERANGE
#define MR_ELOOP                        ELOOP
#define MR_ESPIPE                       ESPIPE
#elif defined(MR_USE_ERRNO_3RD)
/* In 'mrX/platform/libc/mr_errno.h' */
#else
#define MR_EPERM                        (1)
#define MR_ENOENT                       (2)
#define MR_EIO                          (5)
#define MR_ENODEV                       (19)
#define MR_EAGAIN                       (11)
#define MR_ENOMEM                       (12)
#define MR_EBUSY                        (16)
#define MR_EEXIST                       (17)
#define MR_EINVAL                       (22)
#define MR_ENOSYS                       (40)
#define MR_ETIMEDOUT                    (116)
#define MR_ECANCELED                    (125)
#define MR_EBADF                        (9)
#define MR_ENOTDIR                      (20)
#define MR_EISDIR                       (21)
#define MR_ENOTEMPTY                    (39)
#define MR_EMFILE                       (24)
#define MR_ENAMETOOLONG                 (36)
#define MR_EXDEV                        (18)
#define MR_ERANGE                       (34)
#define MR_ELOOP                        (92)
#define MR_ESPIPE                       (29)
#endif /* defined(MR_USE_ERRNO_STD) */

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_LIBC_ERRNO_H__ */
