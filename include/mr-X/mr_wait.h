/**
 * @copyright (c) 2024-2025, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_WAIT_H__
#define __MR_WAIT_H__

#include <mr_config.h>
#if defined(MR_USE_WAIT)
#include <mr-X/mr_object.h>
#include <mr-X/mr_list.h>
#endif /* defined(MR_USE_WAIT) */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup Wait
 * @{
 */

#if defined(MR_USE_WAIT)
/* Wait event definition */
#define MR_WAIT_IN                      (0x10000000U)
#define MR_WAIT_OUT                     (0x20000000U)
#define MR_WAIT_ERR                     (0x40000000U)
#define MR_WAIT_EDGE                    (0x80000000U)

/* Waitable type */
MR_CLAZZ_EXPORT(waitable);
typedef struct mr_waitable {
    mr_object_t parent;
    mr_uint32_t last;
    mr_atomic_t gen;
    mr_list_t list;
    mr_ptr_t check;
    mr_ptr_t param;
} mr_waitable_t;

/* Wait check type */
typedef mr_uint32_t(mr_waitable_check_t)(mr_waitable_t *waitable, void *param);

/* Waiter type */
MR_CLAZZ_EXPORT(waiter);
typedef struct mr_waiter {
    mr_object_t parent;
    mr_uint32_t event;
    mr_list_t list;
    mr_ptr_t entry;
    mr_ptr_t param;
} mr_waiter_t;

/* Wait entry type */
typedef void(mr_waiter_entry_t)(mr_waiter_t *waiter, void *param,
                                mr_uint32_t event);

/**
 * @brief This macro function initializes a waitable.
 *
 * @param _able The waitable.
 * @param _check The waitable check.
 * @param _param The check parameter.
 */
#define MR_WAITABLE_INIT(_able, _check, _param)                                \
    {.parent = MR_OBJECT_INIT(MR_CLAZZ_FIND(waitable)),                        \
     .last = 0,                                                                \
     .gen = MR_ATOMIC_INIT(0),                                                 \
     .list = MR_LIST_INIT(&(_able)->list),                                     \
     .check = (_check),                                                        \
     .param = (_param)}

/**
 * @brief This function initializes a waitable.
 *
 * @param able The waitable.
 * @param check The waitable check.
 * @param param The check parameter.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_waitable_init(mr_waitable_t *able, mr_waitable_check_t *check,
                          void *param);

/**
 * @brief This function creates a waitable.
 *
 * @param check The waitable check.
 * @param param The check parameter.
 * @return The waitable on success, NULL on failure.
 */
mr_waitable_t *mr_waitable_create(mr_waitable_check_t *check, void *param);

/**
 * @brief This function deletes a waitable.
 *
 * @param able The waitable.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_waitable_del(mr_waitable_t *able);

/**
 * @brief This function completes a waitable.
 *
 * @param able The waitable.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_waitable_complete(mr_waitable_t *able);

/**
 * @brief This function raises a waitable.
 *
 * @param able The waitable.
 * @param event The wait event.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_waitable_raise(mr_waitable_t *able, mr_uint32_t event);

/**
 * @brief This function gets a waitable.
 *
 * @param able The waitable.
 * @return The waitable on success, MR_NULL on failure.
 */
MR_INLINE mr_waitable_t *mr_waitable_get(mr_waitable_t *able) {
    /* Get waitable reference */
    return (mr_waitable_t *)mr_object_get((mr_object_t *)able);
}

/**
 * @brief This function puts a waitable.
 *
 * @param able The waitable.
 * @return MR_TRUE on end of life cycle, MR_FALSE otherwise.
 */
MR_INLINE mr_bool_t mr_waitable_put(mr_waitable_t *able) {
    /* Put waitable reference */
    return mr_object_put((mr_object_t *)able);
}

/**
 * @brief This macro function initializes a waiter.
 *
 * @param _wait The waiter.
 * @param _entry The wait entry.
 * @param _param The entry parameter.
 * @param _event The wait event('IN||OUT||ERR').
 */
#define MR_WAITER_INIT(_wait, _entry, _param, _event)                          \
    {.parent = MR_OBJECT_INIT(MR_CLAZZ_FIND(waiter)),                          \
     .event = (_event),                                                        \
     .list = MR_LIST_INIT(&(_wait)->list),                                     \
     .entry = (_entry),                                                        \
     .param = (_param)}

/**
 * @brief This function initializes a waiter.
 *
 * @param wait The waiter.
 * @param entry The wait entry.
 * @param param The entry parameter.
 * @param event The wait event('IN||OUT||ERR').
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_waiter_init(mr_waiter_t *wait, mr_waiter_entry_t *entry,
                        void *param, mr_uint32_t event);

/**
 * @brief This function creates a waiter.
 *
 * @param entry The wait entry.
 * @param param The entry parameter.
 * @param event The wait event('IN||OUT||ERR').
 * @return The waiter on success, NULL on failure.
 */
mr_waiter_t *mr_waiter_create(mr_waiter_entry_t *entry, void *param,
                              mr_uint32_t event);

/**
 * @brief This function deletes a waiter.
 *
 * @param wait The waiter.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_waiter_del(mr_waiter_t *wait);

/**
 * @brief This function checks if a waiter is waiting.
 *
 * @param waiter The waiter.
 * @return MR_TRUE if the waiter is waiting, false otherwise.
 */
#define MR_WAITER_IS_WAITING(_waiter)   (!mr_list_is_empty(&(_waiter)->list))

/**
 * @brief This function waits for a waitable.
 *
 * @param wait The waiter.
 * @param able The waitable.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_waiter_wait(mr_waiter_t *wait, mr_waitable_t *able);

/**
 * @brief This function cancels a waiter.
 *
 * @param wait The waiter.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_waiter_cancel(mr_waiter_t *wait);

/**
 * @brief This function gets waiter waiting event.
 *
 * @param wait The waiter.
 * @return The waiting event.
 */
MR_INLINE mr_uint32_t mr_waiter_event(mr_waiter_t *wait) {
    /* Check parameter */
    MR_ASSERT((wait != MR_NULL) && MR_OBJECT_IS_INITED(wait));
    MR_ASSERT(MR_OBJECT_CLAZZ_IS(wait, waiter));

    /* Return waiter event */
    return wait->event;
}

/**
 * @brief This function sets waiter waiting event.
 *
 * @param wait The waiter.
 * @param event The waiting event.
 * @return 0 on success, a negative error code on failure.
 *
 * @note If the waiter is waiting, it will be canceled.
 */
mr_err_t mr_waiter_event_set(mr_waiter_t *wait, mr_uint32_t event);

/**
 * @brief This function gets a waiter.
 *
 * @param wait The waiter.
 * @return The waiter on success, MR_NULL on failure.
 */
MR_INLINE mr_waiter_t *mr_waiter_get(mr_waiter_t *wait) {
    /* Get waiter reference */
    return (mr_waiter_t *)mr_object_get((mr_object_t *)wait);
}

/**
 * @brief This function puts a waiter.
 *
 * @param wait The waiter.
 * @return MR_TRUE on end of life cycle, MR_FALSE otherwise.
 */
MR_INLINE mr_bool_t mr_waiter_put(mr_waiter_t *wait) {
    /* Put waiter reference */
    return mr_object_put((mr_object_t *)wait);
}
#endif /* defined(MR_USE_WAIT) */

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_WAIT_H__ */
