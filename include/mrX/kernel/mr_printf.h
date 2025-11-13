/**
 * @copyright (c) 2024-2025, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_PRINTF_H__
#define __MR_PRINTF_H__

#include <mrX/mr_config.h>
#if defined(MR_USE_PRINTF)
#include <mrX/libc/mr_errno.h>
#include <mrX/libc/mr_types.h>
#endif /* defined(MR_USE_PRINTF) */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup Printf
 * @{
 */

#if defined(MR_USE_PRINTF)
#if defined(MR_USE_PRINTF_COLOR)
/* Printf color definition */
#define MR_COLOR_RESET                  "\033[0m"
#define MR_COLOR_BLACK                  "\033[30m"
#define MR_COLOR_RED                    "\033[31m"
#define MR_COLOR_GREEN                  "\033[32m"
#define MR_COLOR_YELLOW                 "\033[33m"
#define MR_COLOR_BLUE                   "\033[34m"
#define MR_COLOR(_color, _str)          _color _str MR_COLOR_RESET
#else
#define MR_COLOR_RESET
#define MR_COLOR_BLACK
#define MR_COLOR_RED
#define MR_COLOR_GREEN
#define MR_COLOR_YELLOW
#define MR_COLOR_BLUE
#define MR_COLOR(_color, _str)          _str
#endif /* defined(MR_USE_PRINTF_COLOR) */

/**
 * @brief This function prints a formatted string.
 *
 * @param fmt The format string.
 * @param args The arguments.
 * @return The number of characters printed.
 */
int mr_vprintf(const char *fmt, mr_va_list args);

/**
 * @brief This function prints a formatted string.
 *
 * @param fmt The format string.
 * @param ... The arguments.
 * @return The number of characters printed.
 */
int mr_printf(const char *fmt, ...);
#endif /* defined(MR_USE_PRINTF) */

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_PRINTF_H__ */
