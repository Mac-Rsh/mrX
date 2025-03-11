/**
 * @copyright (c) 2024, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_KWORKQUEUE_H__
#define __MR_KWORKQUEUE_H__

#include <mr_config.h>
#if defined(MR_USE_KWORKQUEUE) && defined(MR_USE_KTIMER)
#include <kernel/mr_ktimer.h>
#endif /* defined(MR_USE_KWORKQUEUE) && defined(MR_USE_KTIMER) */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#if defined(MR_USE_KWORKQUEUE) && defined(MR_USE_KTIMER)
/* Kworkqueue type */
typedef struct mr_kworkqueue {
    mr_kobject_t parent;
    mr_ktimer_t timer;
    mr_atomic_t dying;
    mr_klist_t tlist;
    mr_klist_t list;
#if defined(MR_USE_KWORKQUEUE_HOOK)
    mr_ptr_t suspend;
    mr_ptr_t wakeup;
#endif /* defined(MR_USE_KWORKQUEUE_HOOK) */
} mr_kworkqueue_t;

/* Kwork type */
typedef struct mr_kwork {
    mr_klist_t list;
    mr_ptr_t kqueue;
    mr_tick_t tick;
    mr_ptr_t entry;
    mr_ptr_t args;
} mr_kwork_t;

/**
 * @brief This macro function checks if a kworkqueue is initialized.
 *
 * @param _kqueue The kworkqueue.
 * @return MR_TRUE if the kworkqueue is initialized, MR_FALSE otherwise.
 */
#define MR_KWORKQUEUE_IS_INITED(_kqueue)                                       \
    MR_KOBJECT_IS_INITED(_kqueue)

/**
 * @brief This function initialize a kworkqueue.
 *
 * @param kqueue The kworkqueue.
 * @param name The kworkqueue name.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_kworkqueue_init(mr_kworkqueue_t *kqueue, const char *name);

/**
 * @brief This function create a kworkqueue.
 *
 * @param name The kworkqueue name.
 * @return The kworkqueue on success, MR_NULL on failure.
 */
mr_kworkqueue_t *mr_kworkqueue_create(const char *name);

/**
 * @brief This function delete a kworkqueue.
 *
 * @param kqueue The kworkqueue.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_kworkqueue_del(mr_kworkqueue_t *kqueue);

/**
 * @brief This function execute a kworkqueue.
 *
 * @param kqueue The kworkqueue.
 * return 0 on success, a negative error code on failure.
 */
mr_err_t mr_kworkqueue_execute(mr_kworkqueue_t *kqueue);

/**
 * @brief This macro function checks if a kwork is initialized.
 *
 * @param _kwork The kwork.
 * @return MR_TRUE if the kwork is initialized, MR_FALSE otherwise.
 */
#define MR_KWORK_IS_INITED(_kwork)      ((_kwork)->entry != MR_NULL)

/**
 * @brief This macro function initialize a kwork.
 *
 * @param _kwork The kwork.
 * @param _entry The kwork entry.
 * @param _args The kwork args.
 */
#define MR_KWORK_INIT(_kwork, _entry, _args)                                   \
    {                                                                          \
        .list = MR_KLIST_INIT(&(_kwork)->list), .tick = 0, .entry = (_entry),  \
        .args = (_args)                                                        \
    }

/**
 * @brief This function initialize a kwork.
 *
 * @param kwork The kwork.
 * @param entry The kwork entry.
 * @param args The kwork args.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_kwork_init(mr_kwork_t *kwork,
                       void (*entry)(mr_kwork_t *kwork, void *args),
                       void *args);

/**
 * @brief This function delete a kwork.
 *
 * @param kwork The kwork.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_kwork_del(mr_kwork_t *kwork);

/**
 * @brief This function add a kwork to a kworkqueue.
 *
 * @param kqueue The kworkqueue.
 * @param kwork The kwork.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_kworkqueue_work(mr_kworkqueue_t *kqueue, mr_kwork_t *kwork);

/**
 * @brief This function add a kdelayed-work to a kworkqueue.
 *
 * @param kqueue The kworkqueue.
 * @param kwork The kdelayed-work.
 * @param tick The kdelayed-work tick.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_kworkqueue_delayed_work(mr_kworkqueue_t *kqueue, mr_kwork_t *kwork,
                                    mr_tick_t tick);

/**
 * @brief This function schedule a kwork.
 *
 * @param kwork The kwork.
 * @return 0 on success, a negative error code on failure.
 *
 * @note The kwork must have worked in a kworkqueue and has not been manually
 * deleted.
 */
mr_err_t mr_kwork_schedule(mr_kwork_t *kwork);

/**
 * @brief This function schedule a kdelayed-work.
 *
 * @param kwork The kdelayed-work.
 * @param tick The kdelayed-work tick.
 * @return 0 on success, a negative error code on failure.
 *
 * @note The kwork must have worked in a kworkqueue and has not been manually
 * deleted.
 */
mr_err_t mr_kwork_delayed_schedule(mr_kwork_t *kwork, mr_tick_t tick);

#if defined(MR_USE_KWORKQUEUE_HOOK)
/**
 * @brief This function set a kworkqueue wakeup hook.
 *
 * @param kqueue The kworkqueue.
 * @param hook The kworkqueue wakeup hook.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_kworkqueue_wakeup_hook_set(mr_kworkqueue_t *kqueue,
                                       void (*hook)(mr_kworkqueue_t *));

/**
 * @brief This function set a kworkqueue suspend hook.
 *
 * @param kqueue The kworkqueue.
 * @param hook The kworkqueue suspend hook.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_kworkqueue_suspend_hook_set(mr_kworkqueue_t *kqueue,
                                        void (*hook)(mr_kworkqueue_t *));
#endif /* defined(MR_USE_KWORKQUEUE_HOOK) */

/**
 * @brief This function find a kworkqueue.
 *
 * @param name The kworkqueue name.
 * @return The kworkqueue on success, MR_NULL on failure.
 */
mr_kworkqueue_t *mr_kworkqueue_find(const char *name);

/**
 * @brief This function get a kworkqueue.
 *
 * @param kqueue The kworkqueue.
 * @return The kworkqueue on success, MR_NULL on failure.
 */
MR_INLINE mr_kworkqueue_t *mr_kworkqueue_get(mr_kworkqueue_t *kqueue) {
    return (mr_kworkqueue_t *)mr_kobject_get((mr_kobject_t *)kqueue);
}

/**
 * @brief This function put a kworkqueue.
 *
 * @param kqueue The kworkqueue.
 */
MR_INLINE void mr_kworkqueue_put(mr_kworkqueue_t *kqueue) {
    mr_kobject_put((mr_kobject_t *)kqueue);
}
#endif /* defined(MR_USE_KWORKQUEUE) && defined(MR_USE_KTIMER) */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_KWORKQUEUE_H__ */
