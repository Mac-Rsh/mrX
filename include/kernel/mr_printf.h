/**
 * @copyright (c) 2024, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_KNL_PRINTF_H__
#define __MR_KNL_PRINTF_H__

#include <mr_config.h>
#if defined(MR_USE_PRINTF)
#include <libc/mr_errno.h>
#endif /* defined(MR_USE_PRINTF) */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup Printf
 * @{
 */

#if defined(MR_USE_PRINTF)
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

#endif /* __MR_KNL_PRINTF_H__ */
