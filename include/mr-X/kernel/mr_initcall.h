/**
 * @copyright (c) 2024-2025, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_INITCALL_H__
#define __MR_INITCALL_H__

#include <mr-X/kernel/mr_service.h>
#include <mr-X/libc/mr_compiler.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup InitCall
 * @{
 */

/* Initcall level definition */
#define MR_INIT_LEVEL_IRQ               "1.0"
#define MR_INIT_LEVEL_DRIVER            "1.1"
#define MR_INIT_LEVEL_USER(_level)      "8."_level

/* Initcall entry type */
typedef void(mr_initcall_entry_t)(void);

/* Initcall type */
typedef struct mr_initcall {
    const char *name;
    mr_initcall_entry_t *entry;
} mr_initcall_t;

/**
 * @brief This macro function exports an initcall.
 *
 * @param _entry The initcall function.
 * @param _level The initcall level.
 */
#define MR_INIT_EXPORT(_entry, _level)                                         \
    MR_USED const mr_initcall_t MR_CAT(__mr_initcall_, _entry)                 \
        MR_SECTION_ALIGN("mr_initcall." _level)                                \
        = {.name = MR_STR(_entry), .entry = (_entry)}

/**
 * @brief This function does initcalls(kernel).
 *
 * @note User does not need to call this function.
 */
void mr_kernel_init(void);

/**
 * @brief This function does initcalls(user).
 *
 * @note If the kernel does not start, it will start here.
 */
void mr_user_init(void);

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_INITCALL_H__ */
