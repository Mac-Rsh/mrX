/**
 * @copyright (c) 2024, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_LIBC_SCANF_H__
#define __MR_LIBC_SCANF_H__

#include <mr_config.h>
#if defined(MR_USE_LIBC_SCANF)
#include <stdio.h>
#endif /* defined(MR_USE_LIBC_SCANF) */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup Scanf
 * @{
 */

/* Scanf definition */
#if defined(MR_USE_LIBC_SCANF)
#define mr_sscanf(_b, _fmt, ...)        sscanf(_b, _fmt, __VA_ARGS__)
#else
/**
 * @brief This function scans a formatted string.
 *
 * @param buf The string to scan.
 * @param fmt The format string.
 * @param ... The arguments.
 * @return The characters scanned number on success, -1 on error.
 */
int mr_sscanf(const char *buf, const char *fmt, ...);
#endif /* defined(MR_USE_LIBC_SCANF) */

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_LIBC_SCANF_H__ */
