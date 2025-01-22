/**
 * @copyright (c) 2024, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_CLOCK_H__
#define __MR_CLOCK_H__

#include <libc/mr_errno.h>
#include <libc/mr_types.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup Clock
 * @{
 */

/* Clock type */
typedef mr_uint32_t                     mr_clock_t;
typedef mr_clock_t                      mr_tick_t;
#define MR_CLOCK_MAX                    MR_UINT32_MAX
#define MR_TICK_MAX                     MR_CLOCK_MAX

/**
 * @brief This function increases the kclock.
 *
 * @param clock The clock to increase.
 */
void mr_clock_increase(mr_clock_t clock);

/**
 * @brief This function gets the kclock.
 *
 * @return The kclock.
 */
mr_clock_t mr_clock_get(void);

/**
 * @brief This function sets the kclock.
 *
 * @param clock The kclock to clock.
 */
void mr_clock_set(mr_clock_t clock);

/**
 * @brief This function adds a hooks to the kclock.
 *
 * @param index The index of the hooks(auto: '-1').
 * @param hook The hooks to add.
 * @return The index on success, or a negative error code on failure.
 */
int mr_clock_hook_add(int index, void (*hook)(void));

/**
 * @brief This function deletes a hooks from the kclock.
 *
 * @param index The index of the hooks.
 */
void mr_clock_hook_del(int index);

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_CLOCK_H__ */
