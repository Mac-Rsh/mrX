/**
 * @copyright (c) 2024, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_TIMER_H__
#define __MR_TIMER_H__

#include <kernel/mr_clock.h>
#include <kernel/mr_kobject.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup Timer
 * @{
 */

/* Periodic definition */
#define MR_TIMER_PERIODIC               (1U << 31)

/* Timer type */
typedef struct mr_timer {
    mr_kobject_t parent;
    mr_list_t list;
    mr_tick_t init_tick;
    mr_tick_t timeout_tick;
    mr_ptr_t entry;
    mr_ptr_t args;
} mr_timer_t;

/**
 * @brief This function initialize a timer.
 *
 * @param timer The timer.
 * @param name The timer name.
 * @param entry The timer entry.
 * @param args The timer args.
 * @param tick The timer tick(period: 'tick | MR_TIMER_PERIODIC').
 * @returns 0 on success, or a negative error code on failure.
 */
mr_err_t mr_timer_init(mr_timer_t *timer, const char *name,
                       void (*entry)(mr_timer_t *, void *), void *args,
                       mr_tick_t tick);

/**
 * @brief This function create a timer.
 *
 * @param name The timer name.
 * @param entry The timer entry.
 * @param args The timer args.
 * @param tick The timer tick.
 * @return The timer on success, or MR_NULL on failure.
 */
mr_timer_t *mr_timer_create(const char *name,
                            void (*entry)(mr_timer_t *, void *), void *args,
                            mr_tick_t tick);

/**
 * @brief This function delete a timer.
 *
 * @param timer The timer.
 * @return 0 on success, or a negative error code on failure.
 */
mr_err_t mr_timer_del(mr_timer_t *timer);

/**
 * @brief This function start a timer.
 *
 * @param timer The timer.
 * @return 0 on success, or a negative error code on failure.
 */
mr_err_t mr_timer_start(mr_timer_t *timer);

/**
 * @brief This function stop a timer.
 *
 * @param timer The timer.
 * @return 0 on success, or a negative error code on failure.
 */
mr_err_t mr_timer_stop(mr_timer_t *timer);

/**
 * @brief This function set a timer tick.
 *
 * @param timer The timer.
 * @param tick The timer tick.
 * @return 0 on success, or a negative error code on failure.
 *
 * @note If the timer is running, it will be stopped.
 */
mr_err_t mr_timer_tick_set(mr_timer_t *timer, mr_tick_t tick);

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_TIMER_H__ */
