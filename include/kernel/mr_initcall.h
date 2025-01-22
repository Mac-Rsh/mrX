/**
 * @copyright (c) 2024, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_INITCALL_H__
#define __MR_INITCALL_H__

#include <kernel/mr_compiler.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup InitCall
 * @{
 */

/* Initcall level definition */
#define MR_INIT_LEVEL_KERNEL            "1"
#define MR_INIT_LEVEL_USER(_level)      "2."_level

/* Initcall type */
typedef struct mr_initcall {
    const char *name;
    void (*fn)(void);
} mr_initcall_t;

/**
 * @brief This macro function defines an initcall.
 *
 * @param _fn The function of the initcall.
 * @param _level The level of the initcall.
 */
#define MR_INIT_EXPORT(_fn, _level)                                            \
    MR_USED const mr_initcall_t __mr_initcall_##_fn MR_SECTION(                \
        "mr_initcall." _level)                                                 \
        = {#_fn, _fn}

/**
 * @brief This function does initcalls(kernel).
 *
 * @note User does not need to call this function.
 */
void mr_autoinit_kernel(void);

/**
 * @brief This function does initcalls(user).
 */
void mr_autoinit(void);

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_INITCALL_H__ */
