/**
 * @copyright (c) 2024-2025, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_TIMER_H__
#define __MR_TIMER_H__

#include <mr_config.h>
#if defined(MR_USE_TIMER)
#include <mr-X/mr_object.h>
#include <mr-X/mr_clock.h>
#endif /* defined(MR_USE_TIMER) */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup Timer
 * @{
 */

#if defined(MR_USE_TIMER)
/* Timer periodic definition */
#define MR_TIMER_PERIODIC               (1U << 31)

/* Timer type */
typedef struct mr_timer {
    mr_object_t parent;
    mr_tick_t itick;
    mr_tick_t ttick;
    mr_list_t list;
    mr_ptr_t entry;
    mr_ptr_t param;
} mr_timer_t;

/* Timer entry type */
typedef void(mr_timer_entry_t)(mr_timer_t *timer, void *param);

/**
 * @brief This function initializes a timer.
 *
 * @param timer The timer.
 * @param entry The timer entry.
 * @param param The entry parameter.
 * @param tick The timing tick(period: 'tick | MR_TIMER_PERIODIC').
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_timer_init(mr_timer_t *timer, mr_timer_entry_t *entry, void *param,
                       mr_tick_t tick);

/**
 * @brief This function creates a timer.
 *
 * @param entry The timer entry.
 * @param param The entry parameter.
 * @param tick The timing tick(period: 'tick | MR_TIMER_PERIODIC').
 * @return The timer on success, MR_NULL on failure.
 */
mr_timer_t *mr_timer_create(mr_timer_entry_t *entry, void *param,
                            mr_tick_t tick);

/**
 * @brief This function deletes a timer.
 *
 * @param timer The timer.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_timer_del(mr_timer_t *timer);

/**
 * @brief This function starts a timer.
 *
 * @param timer The timer.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_timer_start(mr_timer_t *timer);

/**
 * @brief This function stops a timer.
 *
 * @param timer The timer.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_timer_stop(mr_timer_t *timer);

/**
 * @brief This function gets timer timing tick.
 *
 * @param timer The timer.
 * @return The timing tick.
 */
mr_tick_t mr_timer_tick(mr_timer_t *timer);

/**
 * @brief This function sets timer timing tick.
 *
 * @param timer The timer.
 * @param tick The timing tick.
 * @return 0 on success, a negative error code on failure.
 *
 * @note If the timer is running, it will be stopped.
 */
mr_err_t mr_timer_tick_set(mr_timer_t *timer, mr_tick_t tick);

/**
 * @brief This function gets a timer.
 *
 * @param timer The timer.
 * @return The timer on success, MR_NULL on failure.
 */
MR_INLINE mr_timer_t *mr_timer_get(mr_timer_t *timer) {
    return (mr_timer_t *)mr_object_get((mr_object_t *)timer);
}

/**
 * @brief This function puts a timer.
 *
 * @param timer The timer.
 * @return MR_TRUE on end of life cycle, MR_FALSE otherwise.
 */
MR_INLINE mr_bool_t mr_timer_put(mr_timer_t *timer) {
    return mr_object_put((mr_object_t *)timer);
}
#endif /* defined(MR_USE_TIMER) */

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_TIMER_H__ */
