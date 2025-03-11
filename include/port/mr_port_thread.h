/**
 * @copyright (c) 2024, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_PORT_THREAD_H__
#define __MR_PORT_THREAD_H__

#include <kernel/mr_kclock.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup Port-thread
 * @{
 */

/**
 * @brief This function initializes a thread.
 *
 * @param thread The thread to be initialized.
 * @param name The thread name.
 * @param kthread The kthread to thread arguments.
 * @param stack The thread stack.
 * @param stack_size The thread stack size.
 * @param priority The thread priority.
 * @param tick The thread time slice.
 * @return 0 on success, a negative error code on failure.
 *
 * @note User implementation.
 */
extern mr_err_t mr_port_thread_init(void *thread, const char *name,
                                    void *kthread, void *stack,
                                    mr_size_t stack_size, mr_uint32_t priority,
                                    mr_tick_t tick);

/**
 * @brief This function creates a thread.
 *
 * @param name The thread name.
 * @param kthread The kthread to thread arguments.
 * @param stack_size The thread stack size.
 * @param priority The thread priority.
 * @param tick The thread time slice.
 * @return The thread on success, MR_NULL on failure.
 *
 * @note User implementation.
 */
extern void *mr_port_thread_create(const char *name, void *kthread,
                                   mr_size_t stack_size, mr_uint32_t priority,
                                   mr_tick_t tick);

/**
 * @brief This function deletes a thread.
 *
 * @param thread The thread to be deleted.
 * @return 0 on success, a negative error code on failure.
 *
 * @note User implementation.
 */
extern mr_err_t mr_port_thread_del(void *thread);

/**
 * @brief This function starts a thread.
 *
 * @param thread The thread to be started.
 * @return 0 on success, a negative error code on failure.
 *
 * @note User implementation.
 */
extern mr_err_t mr_port_thread_startup(void *thread);

/**
 * @brief This function resumes a thread.
 *
 * @param thread The thread to be resumed.
 * @return 0 on success, a negative error code on failure.
 *
 * @note User implementation.
 */
extern mr_err_t mr_port_thread_resume(void *thread);

/**
 * @brief This function suspends the current thread.
 *
 * @return 0 on success, a negative error code on failure.
 *
 * @note User implementation.
 */
extern mr_err_t mr_port_thread_yield(void);

/**
 * @brief This function suspends a thread.
 *
 * @param thread The thread to be suspended.
 * @return 0 on success, a negative error code on failure.
 *
 * @note User implementation.
 */
extern mr_err_t mr_port_thread_suspend(void *thread);

/**
 * @brief This function waits for a thread to sleep.
 *
 * @param tick The thread time slice.
 * @return 0 on success, a negative error code on failure.
 *
 * @note User implementation.
 */
extern mr_err_t mr_port_thread_sleep(mr_tick_t tick);

/**
 * @brief This function returns the current thread arguments.
 *
 * @return The current thread arguments(kthread).
 *
 * @note User implementation.
 */
extern void *mr_port_thread_self(void);

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_PORT_THREAD_H__ */
