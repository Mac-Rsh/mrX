/**
 * @copyright (c) 2024, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_INITCALL_H__
#define __MR_INITCALL_H__

#include <libc/mr_compiler.h>

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
    void (*entry)(void);
} mr_initcall_t;

/**
 * @brief This macro function defines an initcall.
 *
 * @param _entry The function of the initcall.
 * @param _level The level of the initcall.
 */
#define MR_INIT_EXPORT(_entry, _level)                                         \
    MR_USED const mr_initcall_t __mr_initcall_##_entry MR_SECTION(             \
        "mr_initcall." _level)                                                 \
        = {.name = #_entry, .entry = (_entry)}

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
