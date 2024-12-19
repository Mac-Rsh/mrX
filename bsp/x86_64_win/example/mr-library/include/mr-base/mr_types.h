/**
 * @copyright (c) 2024, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_TYPES_H__
#define __MR_TYPES_H__

#include <mr_config.h>
#ifdef MR_USE_LIBC
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#endif /* MR_USE_LIBC */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup Basic types
 * @{
 */

/* Signless integer types */
#ifdef MR_USE_LIBC
typedef int8_t                          mr_int8_t;
typedef int16_t                         mr_int16_t;
typedef int32_t                         mr_int32_t;
typedef int64_t                         mr_int64_t;
#define MR_INT8_MAX                     INT8_MAX
#define MR_INT16_MAX                    INT16_MAX
#define MR_INT32_MAX                    INT32_MAX
#define MR_INT64_MAX                    INT64_MAX
#else
typedef signed char                     mr_int8_t;
typedef signed short                    mr_int16_t;
typedef signed int                      mr_int32_t;
typedef signed long long                mr_int64_t;
#define MR_INT8_MAX                     (127)
#define MR_INT16_MAX                    (32767)
#define MR_INT32_MAX                    (2147483647U)
#define MR_INT64_MAX                    (9223372036854775807LL)
#endif /* MR_USE_LIBC */

/* Unsigned integer types */
#ifdef MR_USE_LIBC
typedef uint8_t                         mr_uint8_t;
typedef uint16_t                        mr_uint16_t;
typedef uint32_t                        mr_uint32_t;
typedef uint64_t                        mr_uint64_t;
typedef size_t                          mr_size_t;
typedef long                            mr_ssize_t;
#define MR_UINT8_MAX                    UINT8_MAX
#define MR_UINT16_MAX                   UINT16_MAX
#define MR_UINT32_MAX                   UINT32_MAX
#define MR_UINT64_MAX                   UINT64_MAX
#else
typedef unsigned char                   mr_uint8_t;
typedef unsigned short                  mr_uint16_t;
typedef unsigned int                    mr_uint32_t;
typedef unsigned long long              mr_uint64_t;
typedef unsigned long long              mr_size_t;
typedef long long                       mr_ssize_t;
#define MR_UINT8_MAX                    (255)
#define MR_UINT16_MAX                   (65535)
#define MR_UINT32_MAX                   (4294967295)
#define MR_UINT64_MAX                   (18446744073709551615ULL)
#endif /* MR_USE_LIBC */

/* Boolean type */
typedef int                             mr_bool_t;
#define MR_FALSE                        (0)
#define MR_TRUE                         (1)

/* Pointer type */
#ifdef MR_USE_LIBC
typedef void *                          mr_ptr_t;
typedef intptr_t                        mr_intptr_t;
typedef uintptr_t                       mr_uintptr_t;
#define MR_NULL                         NULL
#else
typedef void *                          mr_ptr_t;
typedef long long                       mr_intptr_t;
typedef unsigned long long              mr_uintptr_t;
#define MR_NULL                         ((void *)0)
#endif /* MR_USE_LIBC */

/** @} */

/**
 * @addtogroup Extended types
 * @{
 */

typedef int                             mr_err_t;

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_TYPES_H__ */
