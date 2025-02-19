/**
 * @copyright (c) 2024, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_KPRINTF_H__
#define __MR_KPRINTF_H__

#include <mr_config.h>
#if defined(MR_USE_KPRINTF)
#include <libc/mr_errno.h>
#include <libc/mr_printf.h>
#endif /* defined(MR_USE_KPRINTF) */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup Kprintf
 * @{
 */

#if defined(MR_USE_KPRINTF)
/**
 * @brief This function prints a formatted string.
 *
 * @param fmt The format string.
 * @param ... The arguments.
 * @return The number of characters printed.
 */
int mr_kprintf(const char *fmt, ...);

/**
 * @brief This function outputs a formatted string.
 *
 * @param buf The buffer.
 * @param size The buffer size.
 * @return The number of characters printed.
 */
int mr_kprintf_output(const char *buf, mr_size_t size);
#endif /* defined(MR_USE_KPRINTF) */

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_KPRINTF_H__ */
