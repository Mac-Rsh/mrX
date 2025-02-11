/**
 * @copyright (c) 2024, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_PRINTF_H__
#define __MR_PRINTF_H__

#include <mr_config.h>
#if defined(MR_USE_LIBC_PRINTF)
#include <stdio.h>
#else
#include <libc/mr_types.h>
#endif /* defined(MR_USE_LIBC_PRINTF) */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup Stdio
 * @{
 */

/* Stdio definition */
#if defined(MR_USE_LIBC_PRINTF)
#define mr_vprintf(_f, _a)              vprintf(_f, _a)
#define mr_vsnprintf(_b, _s, _f, _a)    vsnprintf(_b, _s, _f, _a)
#else

#endif /* defined(MR_USE_LIBC_PRINTF) */

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_PRINTF_H__ */
