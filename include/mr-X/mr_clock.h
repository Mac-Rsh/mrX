/**
 * @copyright (c) 2024-2025, MacRsh
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

/* Clock tick per-second definition */
#if !defined(MR_CFG_TICK_PER_SECOND)
#define MR_CFG_TICK_PER_SECOND          (1000)
#endif /* !defined(MR_CFG_TICK_PER_SECOND) */

/* Clock type */
typedef mr_uint32_t                     mr_tick_t;
#define MR_TICK_MAX                     MR_UINT32_MAX

/* Clock hook type */
typedef mr_tick_t(mr_clock_hook_t)(void);

/**
 * @brief This function increases the clock tick.
 *
 * @param tick The increased tick.
 */
void mr_clock_increase(mr_tick_t tick);

/**
 * @brief This function gets the clock tick.
 *
 * @return The current tick.
 */
mr_tick_t mr_clock_tick(void);

/**
 * @brief This function gets the clock tick less.
 *
 * @return The current tick less.
 */
mr_tick_t mr_clock_tickless(void);

/**
 * @brief This function adds a hook to the clock.
 *
 * @param entry The hook entry.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_clock_hook_add(mr_clock_hook_t *entry);

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_CLOCK_H__ */
