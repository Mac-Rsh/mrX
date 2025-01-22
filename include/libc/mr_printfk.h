/**
 * @copyright (c) 2024, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_PRINTFK_H__
#define __MR_PRINTFK_H__

#include <mr_config.h>
#if defined(MR_USE_LIBC_PRINTFK)
#include <stdio.h>
#else
#include <libc/mr_types.h>
#endif /* defined(MR_USE_LIBC_PRINTFK) */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup Printf
 * @{
 */

/* Printf definition */
#if defined(MR_USE_LIBC_PRINTFK)
#define mr_printfk(_fmt, ...)           printf(_fmt, ##__VA_ARGS__)
#define mr_snprintfk(_b, _s, _f, ...)   snprintf(_b, _s, _f, ##__VA_ARGS__)
#else
/**
 * @brief This function prints a formatted string.
 *
 * @param fmt The format string.
 * @param ... The arguments.
 * @return The number of characters printed.
 */
int mr_printfk(const char *fmt, ...);

/**
 * @brief This function prints a formatted string.
 *
 * @param buf The buffer.
 * @param size The size of the buffer.
 * @param fmt The format string.
 * @param ... The arguments.
 * @return The number of characters printed.
 */
int mr_snprintfk(char *buf, mr_size_t size, const char *fmt, ...);
#endif /* defined(MR_USE_LIBC_PRINTFK) */

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_PRINTFK_H__ */
