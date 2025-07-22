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
typedef struct mr_waitable {
    mr_object_t parent;
    mr_uint32_t last;
    mr_list_t list;
    mr_ptr_t check;
    mr_ptr_t param;
} mr_waitable_t;

/* Wait check type */
typedef mr_uint32_t(mr_waitable_check_t)(mr_waitable_t *waitable, void *param);

/* Waiter type */
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
 * @brief This function initializes a waitable.
 *
 * @param waitable The waitable.
 * @param check The waitable check.
 * @param param The check parameter.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_waitable_init(mr_waitable_t *waitable, mr_waitable_check_t *check,
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
 * @param waitable The waitable.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_waitable_del(mr_waitable_t *waitable);

/**
 * @brief This function completes a waitable.
 *
 * @param waitable The waitable.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_waitable_complete(mr_waitable_t *waitable);

/**
 * @brief This function initializes a waiter.
 *
 * @param waiter The waiter.
 * @param entry The wait entry.
 * @param param The entry parameter.
 * @param event The wait event('IN||OUT||ERR').
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_waiter_init(mr_waiter_t *waiter, mr_waiter_entry_t *entry,
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
 * @param waiter The waiter.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_waiter_del(mr_waiter_t *waiter);

/**
 * @brief This function checks if a waiter is waiting.
 *
 * @param waiter The waiter.
 * @return MR_TRUE if the waiter is waiting, false otherwise.
 */
#define MR_WAITER_IS_WAITING(_waiter) (!mr_list_is_empty(&(_waiter)->list))

/**
 * @brief This function waits for a waitable.
 *
 * @param waiter The waiter.
 * @param waitable The waitable.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_waiter_wait(mr_waiter_t *waiter, mr_waitable_t *waitable);

/**
 * @brief This function cancels a waiter.
 *
 * @param waiter The waiter.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_waiter_cancel(mr_waiter_t *waiter);
#endif /* defined(MR_USE_WAIT) */

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_WAIT_H__ */
