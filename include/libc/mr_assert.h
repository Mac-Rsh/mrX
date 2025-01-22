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
#if defined(MR_USE_LIBC_ASSERT)
#include <assert.h>
#elif defined(MR_USE_3PARTY_ASSERT)
#include <3party/libc/mr_assert.h>
#elif defined(MR_USE_ASSERT)
#include <libc/mr_printfk.h>
#endif /* defined(MR_USE_LIBC_ASSERT) */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup Assert
 * @{
 */

/* Assert definition */
#if defined(MR_USE_LIBC_ASSERT)
#define MR_ASSERT(_expr)                assert(_expr)
#elif defined(MR_USE_3PARTY_ASSERT)
/* In '3party/libc/mr_assert.h' */
#elif defined(MR_USE_ASSERT)
#define MR_ASSERT(_expr)                                                       \
    do {                                                                       \
        if (!(_expr)) {                                                        \
            mr_printfk("%s:%d: %s\n", __FILE__, __LINE__, #_expr);             \
            while (1) {                                                        \
            }                                                                  \
        }                                                                      \
    } while (0)
#else
#define MR_ASSERT(_expr)                ((void)0)
#endif /* defined(MR_USE_LIBC_ASSERT) */

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_ASSERT_H__ */
