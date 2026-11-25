/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_LIBC_SPRINTF_H__
#define __MR_LIBC_SPRINTF_H__

#include <mrX/mr_config.h>
#if defined(MR_USE_SPRINTF_STD)
#include <stdio.h>
#else
#include <mrX/libc/mr_types.h>
#endif /* defined(MR_USE_SPRINTF_STD) */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup Sprintf
 * @{
 */

/* Printf definition */
#if defined(MR_USE_SPRINTF_STD)
#define mr_vsnprintf                    vsnprintf
#define mr_vsprintf                     vsprintf
#define mr_snprintf                     snprintf
#define mr_sprintf                      sprintf
#else
/* Stream output callback type */
typedef void (*mr_sprintf_out_t)(char c, void *ctx);

/**
 * @brief This function formats a string and emits each character via callback.
 *
 * @param out The output character callback.
 * @param ctx The callback context.
 * @param fmt The format string.
 * @param args The arguments.
 * @return The characters printed number.
 */
int mr_vcbprintf(mr_sprintf_out_t out, void *ctx, const char *fmt,
                 mr_va_list args);

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
#endif /* defined(MR_USE_SPRINTF_STD) */

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_LIBC_SPRINTF_H__ */
