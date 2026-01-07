/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_PORT_CLOCK_H__
#define __MR_PORT_CLOCK_H__

#include <mrX/libc/mr_types.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup Port-clock
 * @{
 */

/**
 * @brief This function sets the high resolution clock tick.
 *
 * @param tick The high resolution tick.
 *
 * @note Platform implementation.
 */
extern void mr_port_clock_hires_tick_set(mr_uint64_t tick);

/**
 * @brief This function gets the high resolution clock tick.
 *
 * @return The current high resolution tick.
 *
 * @note Platform implementation.
 */
extern mr_uint64_t mr_port_clock_hires_tick(void);

/**
 * @brief This function busy waits for a specified number of microseconds.
 *
 * @param us The number of microseconds to wait.
 *
 * @note Platform implementation.
 * @note Don't give up the CPU.
 */
extern void mr_port_clock_busy_wait(mr_uint32_t us);

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_PORT_CLOCK_H__ */
