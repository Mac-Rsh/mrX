/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_WORKQUEUE_H__
#define __MR_WORKQUEUE_H__

#include <mrX/mr_config.h>
#if defined(MR_USE_WORKQUEUE)
#include <mrX/kernel/mr_timer.h>
#endif /* defined(MR_USE_WORKQUEUE) */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup Workqueue
 * @{
 */

#if defined(MR_USE_WORKQUEUE)
#if defined(MR_USE_WORKQUEUE_HOOK)
struct mr_workqueue;

/* Workqueue hook type */
typedef struct mr_workqueue_hook {
    void (*suspend)(struct mr_workqueue *queue, void *param);
    void (*wakeup)(struct mr_workqueue *queue, void *param);
    void *param;
} mr_workqueue_hook_t;
#endif /* defined(MR_USE_WORKQUEUE_HOOK) */

/* Workqueue type */
MR_CLAZZES_EXPORT(workqueue);
typedef struct mr_workqueue {
    mr_object_t parent;
    mr_atomic_t depth;
    mr_atomic_t dying;
    mr_timer_t timer;
    mr_list_t tlist;
    mr_list_t list;
#if defined(MR_USE_WORKQUEUE_HOOK)
    const mr_workqueue_hook_t *hook;
#endif /* defined(MR_USE_WORKQUEUE_HOOK) */
} mr_workqueue_t;

/* Work type */
MR_CLAZZES_EXPORT(work);
typedef struct mr_work {
    mr_object_t parent;
    mr_list_t list;
    mr_ptr_t queue;
    mr_tick_t tick;
    mr_ptr_t entry;
    mr_ptr_t param;
} mr_work_t;

/* Work entry type */
typedef void(mr_work_entry_t)(struct mr_work *work, void *param);

/**
 * @brief This macro function initializes a derived workqueue.
 *
 * @param _queue The workqueue.
 * @param _clazz The workqueue clazz.
 */
#define MR_WORKQUEUE_DERIVE_INIT(_queue, _clazz)                               \
    {                                                                          \
        .parent = MR_OBJECT_INIT(_clazz),                                      \
        .depth = MR_ATOMIC_INIT(0),                                            \
        .dying = MR_ATOMIC_INIT(MR_FALSE),                                     \
        .tlist = MR_LIST_INIT(&(_queue)->tlist),                               \
        .list = MR_LIST_INIT(&(_queue)->list),                                 \
    }

/**
 * @brief This macro function initializes a workqueue.
 *
 * @param _queue The workqueue.
 */
#define MR_WORKQUEUE_INIT(_queue)                                              \
    MR_WORKQUEUE_DERIVE_INIT((_queue),                                         \
                             MR_CLAZZES_STATIK(MR_CLAZZES_FIND(workqueue)))

/**
 * @brief This function initializes a workqueue.
 *
 * @param queue The workqueue.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_workqueue_init(mr_workqueue_t *queue);

/**
 * @brief This function creates a workqueue.
 *
 * @return The workqueue on success, MR_NULL on failure.
 */
mr_workqueue_t *mr_workqueue_create(void);

/**
 * @brief This function deletes a workqueue.
 *
 * @param queue The workqueue.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_workqueue_del(mr_workqueue_t *queue);

/**
 * @brief This function executes a workqueue.
 *
 * @param queue The workqueue.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_workqueue_execute(mr_workqueue_t *queue);

/**
 * @brief This function adds a work to a workqueue.
 *
 * @param queue The workqueue.
 * @param work The work.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_workqueue_work(mr_workqueue_t *queue, mr_work_t *work);

/**
 * @brief This function adds a delayed work to a workqueue.
 *
 * @param queue The workqueue.
 * @param work The work.
 * @param tick The delay tick.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_workqueue_delayed_work(mr_workqueue_t *queue, mr_work_t *work,
                                   mr_tick_t tick);

#if defined(MR_USE_WORKQUEUE_HOOK)
/**
 * @brief This function sets a workqueue hook.
 *
 * @param queue The workqueue.
 * @param hook The workqueue hook.
 * @return 0 on success, a negative error code on failure.
 *
 * @note Recommend to use a semaphore with an initial value of 0 as the hook.
 */
MR_INLINE mr_err_t mr_workqueue_hook_set(mr_workqueue_t *queue,
                                         const mr_workqueue_hook_t *hook) {
    /* Check parameter */
    MR_ASSERT((queue != MR_NULL) && MR_OBJECT_IS_INITED(queue));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(queue, workqueue));
    MR_ASSERT(hook != MR_NULL);

    /* Set workqueue hook */
    queue->hook = hook;
    return 0;
}
#endif /* defined(MR_USE_WORKQUEUE_HOOK) */

/**
 * @brief This function gets a workqueue.
 *
 * @param queue The workqueue.
 * @return The workqueue on success, MR_NULL on failure.
 */
MR_INLINE mr_workqueue_t *mr_workqueue_get(mr_workqueue_t *queue) {
    /* Get workqueue reference */
    return (mr_workqueue_t *)mr_object_get((mr_object_t *)queue);
}

/**
 * @brief This function puts a workqueue.
 *
 * @param queue The workqueue.
 * @return MR_TRUE on end of life cycle, MR_FALSE otherwise.
 */
MR_INLINE mr_bool_t mr_workqueue_put(mr_workqueue_t *queue) {
    /* Put workqueue reference */
    return mr_object_put((mr_object_t *)queue);
}

/**
 * @brief This macro function initializes a derived work.
 *
 * @param _work The work.
 * @param _clazz The work clazz.
 * @param _entry The work entry.
 * @param _param The entry parameter.
 */
#define MR_WORK_DERIVE_INIT(_work, _clazz, _entry, _param)                     \
    {                                                                          \
        .parent = MR_OBJECT_INIT(_clazz),                                      \
        .list = MR_LIST_INIT(&(_work)->list),                                  \
        .queue = MR_NULL,                                                      \
        .tick = 0,                                                             \
        .entry = (_entry),                                                     \
        .param = (_param),                                                     \
    }

/**
 * @brief This macro function initializes a work.
 *
 * @param _work The work.
 * @param _entry The work entry.
 * @param _param The entry parameter.
 */
#define MR_WORK_INIT(_work, _entry, _param)                                    \
    MR_WORK_DERIVE_INIT((_work), MR_CLAZZES_STATIK(MR_CLAZZES_FIND(work)),     \
                        (_entry), (_param))

/**
 * @brief This function initializes a work.
 *
 * @param work The work.
 * @param entry The work entry.
 * @param param The entry parameter.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_work_init(mr_work_t *work, mr_work_entry_t *entry, void *param);

/**
 * @brief This function creates a work.
 *
 * @param entry The work entry.
 * @param param The entry parameter.
 * @return The work on success, MR_NULL on failure.
 */
mr_work_t *mr_work_create(mr_work_entry_t *entry, void *param);

/**
 * @brief This function deletes a work.
 *
 * @param work The work.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_work_del(mr_work_t *work);

/**
 * @brief This function schedules a work.
 *
 * @param work The work.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_work_schedule(mr_work_t *work);

/**
 * @brief This function schedules a delayed work.
 *
 * @param work The work.
 * @param tick The delay tick.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_work_delayed_schedule(mr_work_t *work, mr_tick_t tick);

/**
 * @brief This macro function checks if a work is running.
 *
 * @param _work The work.
 * @return MR_TRUE if the work is running, MR_FALSE otherwise.
 */
#define MR_WORK_IS_RUNNING(_work)       (!mr_list_is_empty(&(_work)->list))

/**
 * @brief This function cancels a work.
 *
 * @param work The work.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_work_cancel(mr_work_t *work);

/**
 * @brief This function binds a work to a workqueue.
 *
 * @param work The work.
 * @param queue The workqueue.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_work_bind(mr_work_t *work, mr_workqueue_t *queue) {
    /* Check parameter */
    MR_ASSERT((work != MR_NULL) && MR_OBJECT_IS_INITED(work));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(work, work));
    MR_ASSERT((queue != MR_NULL) && MR_OBJECT_IS_INITED(queue));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(queue, workqueue));

    /* Set workqueue */
    work->queue = queue;
    return 0;
}

/**
 * @brief This function gets a work.
 *
 * @param work The work.
 * @return The work on success, MR_NULL on failure.
 */
MR_INLINE mr_work_t *mr_work_get(mr_work_t *work) {
    /* Get work reference */
    return (mr_work_t *)mr_object_get((mr_object_t *)work);
}

/**
 * @brief This function puts a work.
 *
 * @param work The work.
 * @return MR_TRUE on end of life cycle, MR_FALSE otherwise.
 */
MR_INLINE mr_bool_t mr_work_put(mr_work_t *work) {
    /* Put work reference */
    return mr_object_put((mr_object_t *)work);
}
#endif /* defined(MR_USE_WORKQUEUE) */

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_WORKQUEUE_H__ */
