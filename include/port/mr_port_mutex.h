/**
 * @copyright (c) 2024, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_PORT_MUTEX_H__
#define __MR_PORT_MUTEX_H__

#include <kernel/mr_kclock.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup Port-mutex
 * @{
 */

/**
 * @brief This function initializes a mutex.
 *
 * @param mutex The mutex to be initialized.
 * @param name The mutex name.
 * @return 0 on success, or a negative error code on failure.
 *
 * @note User implementation.
 */
extern mr_err_t mr_port_mutex_init(void *mutex, const char *name);

/**
 * @brief This function deletes a mutex.
 *
 * @param mutex The mutex to be deleted.
 * @return 0 on success, or a negative error code on failure.
 *
 * @note User implementation.
 */
extern mr_err_t mr_port_mutex_del(void *mutex);

/**
 * @brief This function takes a mutex.
 *
 * @param mutex The mutex to be taken.
 * @param timeout The timeout(tick).
 * @return 0 on success, or a negative error code on failure.
 *
 * @note User implementation.
 */
extern mr_err_t mr_port_mutex_take(void *mutex, mr_tick_t timeout);

/**
 * @brief This function gives a mutex.
 *
 * @param mutex The mutex to be given.
 * @return 0 on success, or a negative error code on failure.
 *
 * @note User implementation.
 */
extern mr_err_t mr_port_mutex_give(void *mutex);

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_PORT_MUTEX_H__ */
