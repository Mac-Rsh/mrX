/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_TIMER_H__
#define __MR_TIMER_H__

#include <mrX/mr_config.h>
#if defined(MR_USE_TIMER)
#include <mrX/kernel/mr_object.h>
#include <mrX/kernel/mr_clock.h>
#include <mrX/kernel/mr_list.h>
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
MR_CLAZZES_EXPORT(timer);
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
 * @brief This macro function initializes a derived timer.
 *
 * @param _timer The timer.
 * @param _clazz The timer clazz.
 * @param _entry The timer entry.
 * @param _param The entry parameter.
 * @param _tick The timing tick(period: 'tick | MR_TIMER_PERIODIC').
 */
#define MR_TIMER_DERIVE_INIT(_timer, _clazz, _entry, _param, _tick)            \
    {                                                                          \
        .parent = MR_OBJECT_INIT(_clazz),                                      \
        .itick = (_tick),                                                      \
        .list = MR_LIST_INIT(&(_timer)->list),                                 \
        .entry = (_entry),                                                     \
        .param = (_param),                                                     \
    }

/**
 * @brief This macro function initializes a timer.
 *
 * @param _timer The timer.
 * @param _entry The timer entry.
 * @param _param The entry parameter.
 * @param _tick The timing tick(period: 'tick | MR_TIMER_PERIODIC').
 */
#define MR_TIMER_INIT(_timer, _entry, _param, _tick)                           \
    MR_TIMER_DERIVE_INIT((_timer), MR_CLAZZES_STATIK(timer), (_entry),         \
                         (_param), (_tick))

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
 *
 * @note If the timer is already running, it will be restarted.
 */
mr_err_t mr_timer_start(mr_timer_t *timer);

/**
 * @brief This macro function checks if a timer is timing.
 *
 * @param timer The timer.
 * @return MR_TRUE on timing, MR_FALSE otherwise.
 */
#define MR_TIMER_IS_TIMING(_timer)      (!mr_list_is_empty(&(_timer)->list))

/**
 * @brief This function stops a timer.
 *
 * @param timer The timer.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_timer_stop(mr_timer_t *timer);

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
 * @brief This function gets timer timing tick.
 *
 * @param timer The timer.
 * @return The timing tick.
 */
MR_INLINE mr_tick_t mr_timer_tick(mr_timer_t *timer) {
    /* Check parameter */
    MR_ASSERT((timer != MR_NULL) && MR_OBJECT_IS_INITED(timer));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(timer, timer));

    /* Return timer tick */
    return timer->itick;
}

/**
 * @brief This function gets a timer.
 *
 * @param timer The timer.
 * @return The timer on success, MR_NULL on failure.
 */
MR_INLINE mr_timer_t *mr_timer_get(mr_timer_t *timer) {
    /* Get timer reference */
    return (mr_timer_t *)mr_object_get((mr_object_t *)timer);
}

/**
 * @brief This function puts a timer.
 *
 * @param timer The timer.
 * @return MR_TRUE on end of life cycle, MR_FALSE otherwise.
 */
MR_INLINE mr_bool_t mr_timer_put(mr_timer_t *timer) {
    /* Put timer reference */
    return mr_object_put((mr_object_t *)timer);
}
#endif /* defined(MR_USE_TIMER) */

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_TIMER_H__ */
