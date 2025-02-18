/**
 * @copyright (c) 2024, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_KTIMER_H__
#define __MR_KTIMER_H__

#include <mr_config.h>
#if defined(MR_USE_KTIMER)
#include <kernel/mr_kclock.h>
#include <kernel/mr_kobject.h>
#endif /* defined(MR_USE_KTIMER) */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup Ktimer
 * @{
 */

#if defined(MR_USE_KTIMER)
/* Periodic definition */
#define MR_KTIMER_PERIODIC              (1U << 31)

/* Ktimer type */
typedef struct mr_ktimer {
    mr_kobject_t parent;
    mr_klist_t list;
    mr_tick_t init_tick;
    mr_tick_t timeout_tick;
    mr_ptr_t entry;
    mr_ptr_t args;
} mr_ktimer_t;

/**
 * @brief This function initialize a ktimer.
 *
 * @param ktimer The ktimer.
 * @param name The ktimer name.
 * @param entry The ktimer entry.
 * @param args The ktimer args.
 * @param tick The ktimer tick(period: 'tick | MR_KTIMER_PERIODIC').
 * @returns 0 on success, or a negative error code on failure.
 */
mr_err_t mr_ktimer_init(mr_ktimer_t *ktimer, const char *name,
                        void (*entry)(mr_ktimer_t *ktimer, void *args),
                        void *args, mr_tick_t tick);

/**
 * @brief This function create a timer.
 *
 * @param name The timer name.
 * @param entry The timer entry.
 * @param args The timer args.
 * @param tick The timer tick.
 * @return The timer on success, or MR_NULL on failure.
 */
mr_ktimer_t *mr_ktimer_create(const char *name,
                              void (*entry)(mr_ktimer_t *ktimer, void *args),
                              void *args, mr_tick_t tick);

/**
 * @brief This function delete a ktimer.
 *
 * @param ktimer The ktimer.
 * @return 0 on success, or a negative error code on failure.
 */
mr_err_t mr_ktimer_del(mr_ktimer_t *ktimer);

/**
 * @brief This function start a ktimer.
 *
 * @param ktimer The ktimer.
 * @return 0 on success, or a negative error code on failure.
 */
mr_err_t mr_ktimer_start(mr_ktimer_t *ktimer);

/**
 * @brief This function stop a ktimer.
 *
 * @param ktimer The ktimer.
 * @return 0 on success, or a negative error code on failure.
 */
mr_err_t mr_ktimer_stop(mr_ktimer_t *ktimer);

/**
 * @brief This function set a ktimer tick.
 *
 * @param ktimer The ktimer.
 * @param tick The ktimer tick.
 * @return 0 on success, or a negative error code on failure.
 *
 * @note If the ktimer is running, it will be stopped.
 */
mr_err_t mr_ktimer_tick_set(mr_ktimer_t *ktimer, mr_tick_t tick);

/**
 * @brief This function find a ktimer.
 *
 * @param name The ktimer name.
 * @return The ktimer on success, or MR_NULL on failure.
 */
mr_ktimer_t *mr_ktimer_find(const char *name);

/**
 * @brief This function get a ktimer.
 *
 * @param ktimer The ktimer.
 * @return The ktimer on success, or MR_NULL on failure.
 */
MR_INLINE mr_ktimer_t *mr_ktimer_get(mr_ktimer_t *ktimer) {
    return (mr_ktimer_t *)mr_kobject_get((mr_kobject_t *)ktimer);
}

/**
 * @brief This function put a ktimer.
 *
 * @param ktimer The ktimer.
 */
MR_INLINE void mr_ktimer_put(mr_ktimer_t *ktimer) {
    mr_kobject_put((mr_kobject_t *)ktimer);
}
#endif /* defined(MR_USE_KTIMER) */

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_KTIMER_H__ */
