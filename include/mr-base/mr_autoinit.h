/**
 * @copyright (c) 2024, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_AUTOINIT_H__
#define __MR_AUTOINIT_H__

#include <mr-base/mr_compiler.h>
#include <mr-base/mr_errno.h>
#include <mr-base/mr_types.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup Autoinit
 * @{
 */

#define MR_INIT_LEVEL_SYSTEM            "1"

/* Initcall type */
typedef struct mr_initcall {
    const char *name;
    mr_err_t (*fn)(void);
} mr_initcall_t;

/**
 * @brief This macro function defines an initcall.
 *
 * @param _fn The function of the initcall.
 * @param _level The level of the initcall.
 */
#define MR_INIT_EXPORT(_fn, _level)                                            \
    MR_USED const mr_initcall_t __mr_initcall_##_fn MR_SECTION(                \
        "mr_autoinit." _level)                                                 \
        = {#_fn, _fn}

/**
 * @brief This function does initcalls.
 */
void mr_autoinit(void);

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_AUTOINIT_H__ */
