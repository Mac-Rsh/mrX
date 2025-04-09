/**
 * @copyright (c) 2024, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_KHOOK_H__
#define __MR_KHOOK_H__

#include <libc/mr_errno.h>
#include <libc/mr_types.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup Khook
 * @{
 */

/* Khook type */
typedef struct mr_khook {
    mr_ptr_t *entry;
    mr_size_t size;
    mr_size_t next;
} mr_khook_t;

/**
 * @brief This macro function initializes a hook.
 *
 * @param _entry The hook entry.
 * @param _size The entry size.
 */
#define MR_KHOOK_INIT(_entry, _size)                                           \
    {.entry = (_entry), .size = (_size), .next = 0}

/**
 * @brief This function initializes a khook.
 *
 * @param khook The khook.
 * @param entry The entry pointers.
 * @param size The entry size.
 *
 * @note The size is the entry number.
 */
void mr_khook_init(mr_khook_t *khook, mr_ptr_t *entry, mr_size_t size);

/**
 * @brief This function add an entry to the khook.
 *
 * @param khook The khook.
 * @param entry The entry.
 * @returns 0 on success, a negative error code on failure.
 */
mr_err_t mr_khook_add(mr_khook_t *khook, mr_ptr_t entry);

/**
 * @brief This macro function iterates through the hook.
 *
 * @param _i The index.
 * @param _entry The entry.
 * @param _type The entry type.
 * @param _hook The entry hook.
 */
#define MR_KHOOK_EACH(_i, _entry, _type, _hook)                                \
    for ((_i) = 0, (_entry) = (_type)(_hook)->entry[(_i)];                     \
         (_i) < (_hook)->next; (_i)++, (_entry) = (_type)(_hook)->entry[(_i)])

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_KHOOK_H__ */
