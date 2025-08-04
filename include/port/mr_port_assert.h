/**
 * @copyright (c) 2024-2025, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_PORT_ASSERT_H__
#define __MR_PORT_ASSERT_H__

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup Port-assert
 * @{
 */

/**
 * @brief This function asserts that a condition is true.
 *
 * @param file The file name.
 * @param line The line number.
 * @param expr The condition.
 *
 * @note User implementation.
 */
extern void mr_port_assert(const char *file, int line, const char *expr);

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_PORT_ASSERT_H__ */
