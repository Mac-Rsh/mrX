/**
 * @copyright (c) 2024-2026, MacRsh
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
#define MR_ALIGN(_x)                    __attribute__((aligned(_x)))
#define MR_SECTION(_x)                  __attribute__((section(_x)))
#define MR_UNUSED                       __attribute__((unused))
#define MR_USED                         __attribute__((used))
#define MR_WEAK                         __attribute__((weak))
#define MR_INLINE                       static __inline
#define MR_PACKED                       __attribute__((packed))
#define MR_MAY_ALIAS                    __attribute__((may_alias))
#else
#error "Compiler not supported"
#endif /* defined(__GNUC__) || defined(__clang__) || defined(__ARMCC_VERSION) */
#define MR_SECTION_ALIGN(_x)            MR_ALIGN(MR_ALIGN_SIZE) MR_SECTION(_x)
#define MR_ALIGN_SIZE                   (sizeof(void *))
#define MR_IGNORE(_x)                   (void)(_x)

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_LIBC_COMPILER_H__ */
