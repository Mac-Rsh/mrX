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
#ifdef MR_USE_LIBC_TYPES
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#endif /* MR_USE_LIBC_TYPES */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup Types
 * @{
 */

/* Signed integer type */
#ifdef MR_USE_LIBC_TYPES
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
typedef long long                       mr_ssize_t;
#define MR_INT8_MAX                     (127)
#define MR_INT16_MAX                    (32767)
#define MR_INT32_MAX                    (2147483647U)
#define MR_INT64_MAX                    (9223372036854775807LL)
#endif /* MR_USE_LIBC_TYPES */

/* Unsigned integer type */
#ifdef MR_USE_LIBC_TYPES
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
#define MR_UINT8_MAX                    (255)
#define MR_UINT16_MAX                   (65535)
#define MR_UINT32_MAX                   (4294967295)
#define MR_UINT64_MAX                   (18446744073709551615ULL)
#endif /* MR_USE_LIBC_TYPES */

/* Boolean type */
#ifdef MR_USE_LIBC_TYPES
typedef bool                            mr_bool_t;
#define MR_FALSE                        false
#define MR_TRUE                         true
#else
typedef int                             mr_bool_t;
#define MR_FALSE                        (0)
#define MR_TRUE                         (1)
#endif /* MR_USE_LIBC_TYPES */

/* Pointer type */
#ifdef MR_USE_LIBC_TYPES
typedef void *                          mr_ptr_t;
typedef intptr_t                        mr_intptr_t;
typedef uintptr_t                       mr_uintptr_t;
#define MR_NULL                         NULL
#else
typedef void *                          mr_ptr_t;
typedef long long                       mr_intptr_t;
typedef unsigned long long              mr_uintptr_t;
#define MR_NULL                         ((void *)0)
#endif /* MR_USE_LIBC_TYPES */

/* Floating point type */
typedef float                           mr_f32_t;
typedef double                          mr_f64_t;

/* Variable argument list type */
#ifdef MR_USE_LIBC_TYPES
typedef va_list                         mr_va_list;
#define mr_va_start(_ap, _n)            va_start(_ap, _n)
#define mr_va_arg(_ap, _t)              va_arg(_ap, _t)
#define mr_va_end(_ap)                  va_end(_ap)
#else
typedef char *                          mr_va_list;
#define __mr_va_rounded_size(_t)                                               \
    (((sizeof(_t) + sizeof(int) - 1) / sizeof(int)) * sizeof(int))
#define mr_va_start(_ap, _n)                                                   \
    ((_ap) = (char *)&(_n) + __mr_va_rounded_size(_n))
#define mr_va_arg(_ap, _type)                                                  \
    ((_ap) += __mr_va_rounded_size(_type),                                     \
     *((_type *)((_ap)-__mr_va_rounded_size(_type))))
#define mr_va_end(_ap)                  ((_ap) = (mr_va_list)0)
#endif /* MR_USE_LIBC_TYPES */

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_TYPES_H__ */
