/**
 * @copyright (c) 2024-2025, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_ASYNC_H__
#define __MR_ASYNC_H__

#include <mr_config.h>
#if defined(MR_USE_ASYNC)
#include <mr-X/mr_workqueue.h>
#endif /* defined(MR_USE_ASYNC) */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup Async
 * @{
 */

#if defined(MR_USE_ASYNC)
/* Await reference definition */
#define MR_AWAIT_INFINITE               (mr_uint32_t)(-1)

/* Await type */
typedef struct mr_await {
    mr_object_t parent;
    mr_uint32_t ref;
    mr_list_t list;
} mr_await_t;

/* Async type */
typedef struct mr_async {
    mr_object_t parent;
    mr_await_t await;
    mr_int32_t label;
    mr_list_t list;
    mr_work_t work;
    mr_ptr_t entry;
} mr_async_t;

/* Async entry type */
typedef void(mr_async_entry_t)(mr_async_t *async, void *param);

/**
 * @brief This function initializes an await.
 *
 * @param await The await.
 * @param ref The await reference(Infinite: 'MR_AWAIT_INFINITE').
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_await_init(mr_await_t *await, mr_uint32_t ref);

/**
 * @brief This function creates an await.
 *
 * @param ref The await reference(Infinite: 'MR_AWAIT_INFINITE').
 * @return The await on success, MR_NULL on failure.
 */
mr_await_t *mr_await_create(mr_uint32_t ref);

/**
 * @brief This function deletes an await.
 *
 * @param await The await.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_await_del(mr_await_t *await);

/**
 * @brief This function adds an async to an await.
 *
 * @param await The await.
 * @param async The async.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_await_wait(mr_await_t *await, mr_async_t *async);

/**
 * @brief This function completes an await.
 *
 * @param await The await.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_await_complete(mr_await_t *await);

/**
 * @brief This function gets an await.
 *
 * @param await The await.
 * @return The await on success, MR_NULL on failure.
 */
MR_INLINE mr_await_t *mr_await_get(mr_await_t *await) {
    /* Get await reference */
    return (mr_await_t *)mr_object_get((mr_object_t *)await);
}

/**
 * @brief This function puts an await.
 *
 * @param await The await.
 * @return MR_TRUE on end of life cycle, MR_FALSE otherwise.
 */
MR_INLINE mr_bool_t mr_await_put(mr_await_t *await) {
    /* Put await reference */
    return mr_object_put((mr_object_t *)await);
}

/**
 * @brief This function initializes an async.
 *
 * @param async The async.
 * @param entry The async entry.
 * @param param The async parameter.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_async_init(mr_async_t *async, mr_async_entry_t *entry, void *param);

/**
 * @brief This function creates an async.
 *
 * @param entry The async entry.
 * @param param The async parameter.
 * @return The async on success, MR_NULL on failure.
 */
mr_async_t *mr_async_create(mr_async_entry_t *entry, void *param);

/**
 * @brief This function deletes an async.
 *
 * @param async The async.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_async_del(mr_async_t *async);

/**
 * @brief This macro function binds an async to a workqueue.
 *
 * @param _async The async.
 * @param _queue The workqueue.
 */
#define mr_async_bind(_async, _queue)                                          \
    mr_workqueue_bind(_queue, &(_async)->work)

/**
 * @brief This macro function checks if an async is waiting.
 *
 * @param _async The async.
 */
#define MR_ASYNC_IS_WAITING(_async)     (!mr_list_is_empty(&(_async)->list))

/**
 * @brief This function cancels an async.
 *
 * @param async The async.
 * @return 0 on success, a negative error code on failure.
 *
 * @note All async-io may generate asynchronous behaviors, which can be cancelled by this function.
 */
mr_err_t mr_async_cancel(mr_async_t *async);

/**
 * @brief This macro function waits for an async.
 *
 * @param _async The async.
 * @param _wait_async The async to wait.
 *
 * @note It can generate asynchronous behaviors.
 */
#define mr_async_wait_async(_async, _wait_async)                               \
    mr_await_wait(&(_wait_async)->await, _async)

#if defined(MR_USE_ASYNC_SYNC)
/**
 * @brief This macro function runs an async.
 *
 * @param _async The async.
 * @param _queue The workqueue.
 */
#define mr_async_run(_async, _queue)                                           \
    mr_workqueue_work(_queue, &(_async)->work)

/**
 * @brief This macro function begins an async.
 *
 * @param _async The async.
 */
#define mr_async_begin(_async)                                                 \
    switch ((_async)->label) {                                                 \
        case 0:                                                                \
            while (0)

/**
 * @brief This macro function ends an async.
 *
 * @param _async The async.
 */
#define mr_async_end(_async)                                                   \
    (_async)->label = -1;                                                      \
    mr_await_complete(&(_async)->await);                                       \
    }                                                                          \
    while (0)

/**
 * @brief This macro function yields an async.
 *
 * @param _async The async.
 *
 * @note It can only be used between 'mr_async_begin' and 'mr_async_end'.
 */
#define mr_async_yield(_async)                                                 \
    do {                                                                       \
        mr_work_schedule(&(_async)->work);                                     \
        (_async)->label = __LINE__;                                            \
        return;                                                                \
        case __LINE__:;                                                        \
    } while (0)

/**
 * @brief This macro function sleeps an async.
 *
 * @param _async The async.
 * @param _tick The async tick.
 *
 * @note It can only be used between 'mr_async_begin' and 'mr_async_end'.
 */
#define mr_async_sleep(_async, _tick)                                          \
    do {                                                                       \
        mr_work_delayed_schedule(&(_async)->work, _tick);                      \
        (_async)->label = __LINE__;                                            \
        return;                                                                \
        case __LINE__:;                                                        \
    } while (0)

/**
 * @brief This macro function waits for an async.
 *
 * @param _async The async.
 * @param _lambda The async lambda.
 *
 * @note It can only be used between 'mr_async_begin' and 'mr_async_end'.
 */
#define mr_async_wait(_async, _lambda)                                         \
    do {                                                                       \
        case __LINE__:;                                                        \
            (_lambda);                                                         \
            if (MR_ASYNC_IS_WAITING(_async)) {                                 \
                (_async)->label = __LINE__;                                    \
                return;                                                        \
            }                                                                  \
    } while (0)

/**
 * @brief This macro function waits for an async with a timeout.
 *
 * @param _async The async.
 * @param _lambda The async lambda.
 * @param _timeout The async timeout.
 *
 * @note It can only be used between 'mr_async_begin' and 'mr_async_end'.
 */
#define mr_async_wait_for(_async, _lambda, _timeout)                           \
    do {                                                                       \
        case __LINE__:;                                                        \
            if (!MR_ASYNC_IS_WAITING(_async)) {                                \
                (_lambda);                                                     \
                if (MR_ASYNC_IS_WAITING(_async)) {                             \
                    mr_work_delayed_schedule(&(_async)->work, _timeout);       \
                    (_async)->label = __LINE__;                                \
                    return;                                                    \
                }                                                              \
            } else {                                                           \
                mr_async_cancel(_async);                                       \
            }                                                                  \
    } while (0)
#endif /* defined(MR_USE_ASYNC_SYNC) */

/**
 * @brief This function gets an async.
 *
 * @param async The async.
 * @return The async on success, MR_NULL on failure.
 */
MR_INLINE mr_async_t *mr_async_get(mr_async_t *async) {
    /* Get async reference */
    return (mr_async_t *)mr_object_get((mr_object_t *)async);
}

/**
 * @brief This function puts an async.
 *
 * @param async The async.
 * @return MR_TRUE on end of life cycle, MR_FALSE otherwise.
 */
MR_INLINE mr_bool_t mr_async_put(mr_async_t *async) {
    /* Put async reference */
    return mr_object_put((mr_object_t *)async);
}
#endif /* defined(MR_USE_ASYNC) */

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_ASYNC_H__ */
