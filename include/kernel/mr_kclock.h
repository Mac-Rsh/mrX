/**
 * @copyright (c) 2024, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_KCLOCK_H__
#define __MR_KCLOCK_H__

#include <libc/mr_errno.h>
#include <libc/mr_types.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup Kclock
 * @{
 */

/* Clock type */
typedef mr_uint32_t                     mr_tick_t;
#define MR_TICK_MAX                     MR_UINT32_MAX

/* Clock hook type */
typedef void (mr_kclock_hook_t)(void);

/**
 * @brief This function increases the clock tick.
 *
 * @param tick The tick to increase.
 */
void mr_kclock_increase(mr_tick_t tick);

/**
 * @brief This function gets the clock tick.
 *
 * @return The tick.
 */
mr_tick_t mr_kclock_tick(void);

/**
 * @brief This function adds a hook to the clock.
 * 
 * @param entry The hook entry.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_kclock_hook_add(mr_kclock_hook_t *entry);

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_KCLOCK_H__ */
