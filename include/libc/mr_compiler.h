/**
 * @copyright (c) 2024, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_LIBC_COMPILER_H__
#define __MR_LIBC_COMPILER_H__

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup Compiler
 * @{
 */

/* Compiler specific attributes */
#if defined(__GNUC__) || defined(__clang__) || defined(__ARMCC_VERSION)
#define MR_SECTION(_x)                  __attribute__((section(_x)))
#define MR_USED                         __attribute__((used))
#define MR_WEAK                         __attribute__((weak))
#define MR_INLINE                       static __inline
#else
#error "Compiler not supported"
#endif /* defined(__GNUC__) || defined(__clang__) || defined(__ARMCC_VERSION) */
#define MR_UNUSED(_x)                   (void)(_x)

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_LIBC_COMPILER_H__ */
