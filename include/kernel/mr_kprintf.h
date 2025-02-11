/**
 * @copyright (c) 2024, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_KPRINTF_H__
#define __MR_KPRINTF_H__

#include <libc/mr_printf.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup Kprintf
 * @{
 */

/**
 * @brief This function prints a formatted string.
 *
 * @param fmt The format string.
 * @param ... The arguments.
 * @return The number of characters printed.
 */
int mr_kprintf(const char *fmt, ...);

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_KPRINTF_H__ */
