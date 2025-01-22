/**
 * @copyright (c) 2024, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_HOOKS_H__
#define __MR_HOOKS_H__

#include <kernel/mr_spinlock.h>
#include <libc/mr_errno.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup Hook
 * @{
 */

/* Hooks type */
typedef struct mr_hooks {
    mr_spinlock_t lock;
    mr_ptr_t *hook;
    mr_size_t size;
} mr_hooks_t;

/**
 * @brief This macro function initialize a hooks.
 *
 * @param _hooks The hooks.
 * @param _size The size of the hook pointers.
 */
#define MR_HOOKS_INIT(_hooks, _size)                                           \
    { MR_SPINLOCK_INIT(), (_hooks), (_size) }

/**
 * @brief This function initialize a hooks.
 *
 * @param hooks The hooks.
 * @param hook The hook pointers.
 * @param size The size of the hook pointers.
 *
 * @note The size is the number of the hook pointers.
 */
void mr_hooks_init(mr_hooks_t *hooks, mr_ptr_t *hook, mr_size_t size);

/**
 * @brief This function add a hook.
 *
 * @param hooks The hooks.
 * @param index The index of the hook(auto: '-1').
 * @param hook The hook.
 * @return The index on success, or a negative error code on failure.
 */
int mr_hooks_add(mr_hooks_t *hooks, int index, mr_ptr_t hook);

/**
 * @brief This function delete a hook.
 *
 * @param hooks The hooks.
 * @param index The index of the hook.
 */
void mr_hooks_del(mr_hooks_t *hooks, int index);

/**
 * @brief This macro function iterates through the hooks.
 *
 * @param _i The index.
 * @param _hooks The hooks.
 * @param _hook The hook.
 */
#define MR_HOOKS_EACH(_i, _hooks, _hook)                                       \
    for ((_i) = 0, (_hook) = (_hooks)->hook[(_i)]; (_i) < (_hooks)->size;      \
         (_i)++, (_hook) = (_hooks)->hook[(_i)])

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_HOOKS_H__ */
