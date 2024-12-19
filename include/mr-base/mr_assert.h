/**
 * @copyright (c) 2024, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_ASSERT_H__
#define __MR_ASSERT_H__

#include <mr_config.h>
#ifdef MR_USE_LIBC
#ifndef MR_USE_DEBUG
#define NDEBUG
#endif /* MR_USE_DEBUG */
#include <assert.h>
#endif /* MR_USE_LIBC */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup Assert
 * @{
 */

/* Assert definition */
#ifdef MR_USE_DEBUG
#ifdef MR_USE_LIBC
#define MR_ASSERT(_expr)                assert(_expr)
#else
#define MR_ASSERT(_expr)                ((void)0)
#endif /* MR_USE_LIBC */
#else
#define MR_ASSERT(_expr)
#endif /* MR_USE_DEBUG */

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_ASSERT_H__ */
