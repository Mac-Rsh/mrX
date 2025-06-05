/**
 * @copyright (c) 2024, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_LIBC_SPRINTF_H__
#define __MR_LIBC_SPRINTF_H__

#include <mr_config.h>
#if defined(MR_USE_LIBC_SPRINTF)
#include <stdio.h>
#else
#include <libc/mr_types.h>
#endif /* defined(MR_USE_LIBC_SPRINTF) */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup Printf
 * @{
 */

/* Printf definition */
#if defined(MR_USE_LIBC_SPRINTF)
#define mr_vsnprintf(_b, _s, _f, _a)    vsnprintf(_b, _s, _f, _a)
#define mr_vsprintf(_b, _f, _a)         vsprintf(_b, _f, _a)
#define mr_snprintf(_b, _s, _f, ...)    snprintf(_b, _s, _f, __VA_ARGS__)
#define mr_sprintf(_b, _f, ...)         sprintf(_b, _f, __VA_ARGS__)
#else
/**
 * @brief This function prints a formatted string to a buffer.
 *
 * @param buf The buffer.
 * @param size The buffer size.
 * @param fmt The format string.
 * @param args The arguments.
 * @return The characters printed number on success, -1 on error.
 */
int mr_vsnprintf(char *buf, mr_size_t size, const char *fmt, mr_va_list args);

/**
 * @brief This function prints a formatted string to a buffer.
 *
 * @param buf The buffer.
 * @param fmt The format string.
 * @param args The arguments.
 * @return The characters printed number on success, -1 on error.
 */
int mr_vsprintf(char *buf, const char *fmt, mr_va_list args);

/**
 * @brief This function prints a formatted string to a buffer.
 *
 * @param buf The buffer.
 * @param size The buffer size.
 * @param fmt The format string.
 * @param ... The arguments.
 * @return The characters printed number on success, -1 on error.
 */
int mr_snprintf(char *buf, mr_size_t size, const char *fmt, ...);

/**
 * @brief This function prints a formatted string to a buffer.
 *
 * @param buf The buffer.
 * @param fmt The format string.
 * @param ... The arguments.
 * @return The characters printed number on success, -1 on error.
 */
int mr_sprintf(char *buf, const char *fmt, ...);
#endif /* defined(MR_USE_LIBC_SPRINTF) */

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_LIBC_SPRINTF_H__ */
