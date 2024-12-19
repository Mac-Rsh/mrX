/**
 * @copyright (c) 2024, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_FFS_H__
#define __MR_FFS_H__

#include <mr-base/mr_types.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup FFS
 * @{
 */

/**
 * @brief This function get the position of the first bit set.
 *
 * @param val The value.
 * @return The position of the first bit set.
 *
 * @note Ref: RT-Thread("https://www.rt-thread.org/")
 */
mr_uint32_t mr_ffs(mr_uint32_t val);

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_FFS_H__ */
