/**
 * @copyright (c) 2024-2025, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_PORT_OUTPUT_H__
#define __MR_PORT_OUTPUT_H__

#include <mr-X/libc/mr_types.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup Port-output
 * @{
 */

/**
 * @brief This function output the data.
 *
 * @param buf The buffer.
 * @param size The buffer size.
 * @return The number of characters printed.
 *
 * @note User implementation.
 */
extern int mr_port_output(const char *buf, mr_size_t size);

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_PORT_OUTPUT_H__ */
