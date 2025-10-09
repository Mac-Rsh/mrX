/**
 * @copyright (c) 2024-2025, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_HOOK_H__
#define __MR_HOOK_H__

#include <libc/mr_compiler.h>
#include <libc/mr_errno.h>
#include <libc/mr_types.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup Hook
 * @{
 */

/* Hook type */
typedef struct mr_hook {
    mr_ptr_t *entry;
    mr_uint16_t size;
    mr_uint16_t next;
} mr_hook_t;

/**
 * @brief This macro function initializes a hook.
 *
 * @param _entry The hook entry.
 * @param _size The entry size.
 */
#define MR_HOOK_INIT(_entry, _size)                                            \
    {.entry = (_entry), .size = (_size), .next = 0}

/**
 * @brief This function initializes a hook.
 *
 * @param hook The hook.
 * @param entry The entry pointers.
 * @param size The entry size.
 *
 * @note The size is the entry number.
 */
MR_INLINE void mr_hook_init(mr_hook_t *hook, mr_ptr_t *entry,
                            mr_uint16_t size) {
    /* Initialize hook */
    hook->entry = entry;
    hook->size = size;
    hook->next = 0;
}

/**
 * @brief This function add an entry to the hook.
 *
 * @param hook The hook.
 * @param entry The entry.
 * @returns 0 on success, a negative error code on failure.
 */
mr_err_t mr_hook_add(mr_hook_t *hook, mr_ptr_t entry);

/**
 * @brief This macro function iterates through the hook.
 *
 * @param _i The index.
 * @param _entry The entry.
 * @param _type The entry type.
 * @param _hook The entry hook.
 */
#define MR_HOOK_EACH(_i, _entry, _type, _hook)                                 \
    for ((_i) = 0, (_entry) = (_type *)(_hook)->entry[(_i)];                   \
         (_i) < (_hook)->next;                                                 \
         (_i)++, (_entry) = (_type *)(_hook)->entry[(_i)])

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_HOOK_H__ */
