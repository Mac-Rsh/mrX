/**
 * @copyright (c) 2024-2025, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_LIBC_ASSERT_H__
#define __MR_LIBC_ASSERT_H__

#include <mrX/mr_config.h>
#if defined(MR_USE_ASSERT_STD)
#include <assert.h>
#elif defined(MR_USE_ASSERT_3RD)
#include <3party/libc/mr_assert.h>
#elif defined(MR_USE_ASSERT)
#include <mrX/port/mr_port_assert.h>
#endif /* defined(MR_USE_ASSERT_STD) */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup Assert
 * @{
 */

/* Assert definition */
#if defined(MR_USE_ASSERT_STD)
#define MR_ASSERT(_expr)                assert(_expr)
#define MR_PANIC()                      assert(0)
#elif defined(MR_USE_ASSERT_3RD)
/* In '3party/libc/mr_assert.h' */
#elif defined(MR_USE_ASSERT)
#define MR_ASSERT(_expr)                                                       \
    do {                                                                       \
        if (!(_expr)) {                                                        \
            mr_port_assert(__FILE__, __LINE__, #_expr);                        \
            while (1) {                                                        \
            }                                                                  \
        }                                                                      \
    } while (0)
#define MR_PANIC()                                                             \
    do {                                                                       \
        mr_port_assert(__FILE__, __LINE__, "panic");                           \
        while (1) {                                                            \
        }                                                                      \
    } while (0)
#else
#define MR_ASSERT(_expr)                ((void)0)
#define MR_PANIC()                                                             \
    do {                                                                       \
        while (1) {                                                            \
        }                                                                      \
    } while (0)
#endif /* defined(MR_USE_ASSERT_STD) */

/**
 * @brief This macro function defines a static assert.
 *
 * @param _name The name.
 * @param _expr The expression.
 */
#define MR_STATIC_ASSERT(_name, _expr)                                         \
    typedef char __mr_static_##_name[(_expr) ? 1 : -1]

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_LIBC_ASSERT_H__ */
