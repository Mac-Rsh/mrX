/**
 * @copyright (c) 2024, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_KTHREAD_H__
#define __MR_KTHREAD_H__

#include <mr_config.h>
#if defined(MR_USE_KOS)
#include <kernel/mr_kclock.h>
#include <kernel/mr_kobject.h>
#endif /* defined(MR_USE_KOS) */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup Kthread
 * @{
 */

#if defined(MR_USE_KOS)
/* Kthread type */
typedef struct mr_kthread {
    mr_kobject_t parent;
    mr_ptr_t thread;
#if !defined(MR_CFG_KTHREAD_ITHREAD_SIZE)
#define MR_CFG_KTHREAD_ITHREAD_SIZE     (256)
#endif /* !defined(MR_CFG_KTHREAD_ITHREAD_SIZE) */
    mr_uint8_t ithread[MR_CFG_KTHREAD_ITHREAD_SIZE];
    mr_ptr_t entry;
    mr_ptr_t args;
} mr_kthread_t;

/**
 * @brief This macro function checks if a kthread is initialized.
 *
 * @param _kth The kthread.
 * @return MR_TRUE if the kthread is initialized, MR_FALSE otherwise.
 */
#define MR_KTHREAD_IS_INITED(_kth)      MR_KOBJECT_IS_INITED(_kth)

/**
 * @brief This function initialize a kthread.
 *
 * @param kth The kthread.
 * @param name The kthread name.
 * @param entry The kthread entry.
 * @param args The kthread arguments.
 * @param stack The kthread stack.
 * @param stack_size The kthread stack size.
 * @param priority The kthread priority.
 * @param tick The kthread time slice.
 * @return 0 on success, or a negative error code on failure.
 */
mr_err_t mr_kthread_init(mr_kthread_t *kth, const char *name,
                         void (*entry)(mr_kthread_t *kth, void *args),
                         void *args, void *stack, mr_size_t stack_size,
                         mr_uint32_t priority, mr_tick_t tick);

/**
 * @brief This function create a kthread.
 *
 * @param name The kthread name.
 * @param entry The kthread entry.
 * @param args The kthread arguments.
 * @param stack_size The kthread stack size.
 * @param priority The kthread priority.
 * @param tick The kthread time slice.
 * @return The kthread on success, or MR_NULL on failure.
 */
mr_kthread_t *mr_kthread_create(const char *name,
                                void (*entry)(mr_kthread_t *kth, void *args),
                                void *args, mr_size_t stack_size,
                                mr_uint32_t priority, mr_tick_t tick);

/**
 * @brief This macro function checks if a kthread is a takeover.
 * @param _kth The kthread.
 * @return MR_TRUE if the kthread is a takeover, MR_FALSE otherwise.
 */
#define MR_KTHREAD_IS_TAKEOVER(_kth)    (!((mr_kthread_t *)(_kth))->entry)

/**
 * @brief This function takeover a kthread.
 *
 * @param kth The kthread.
 * @param name The kthread name.
 * @param thread The takeover thread.
 * @return 0 on success, or a negative error code on failure.
 */
mr_err_t mr_kthread_takeover(mr_kthread_t *kth, const char *name, void *thread);

/**
 * @brief This function delete a kthread.
 *
 * @param kth The kthread.
 * @return 0 on success, or a negative error code on failure.
 *
 * @note If kthread is MR_NULL, delete kthread self.
 */
mr_err_t mr_kthread_del(mr_kthread_t *kth);

/**
 * @brief This function startup a kthread.
 *
 * @param kth The kthread.
 * @return 0 on success, or a negative error code on failure.
 */
mr_err_t mr_kthread_startup(mr_kthread_t *kth);

/**
 * @brief This function resume a kthread.
 *
 * @param kth The kthread.
 * @return 0 on success, or a negative error code on failure.
 */
mr_err_t mr_kthread_resume(mr_kthread_t *kth);

/**
 * @brief This function yield the current kthread.
 *
 * @return 0 on success, or a negative error code on failure.
 */
mr_err_t mr_kthread_yield(void);

/**
 * @brief This function suspend a kthread.
 *
 * @param kth The kthread.
 * @return 0 on success, or a negative error code on failure.
 *
 * @note If kthread is MR_NULL, suspend kthread self.
 */
mr_err_t mr_kthread_suspend(mr_kthread_t *kth);

/**
 * @brief This function sleep a kthread.
 *
 * @param tick The sleep time slice.
 * @return 0 on success, or a negative error code on failure.
 */
mr_err_t mr_kthread_sleep(mr_tick_t tick);

/**
 * @brief This function get the current kthread.
 *
 * @return The current kthread.
 */
mr_kthread_t *mr_kthread_self(void);

/**
 * @brief This function is the kthread entry.
 *
 * @param kth The kthread.
 *
 * @note User does not need to call this function.
 * @note Developer adaptation required.
 */
void mr_kthread_entry(mr_kthread_t *kth);

/**
 * @brief This function find a kthread.
 *
 * @param name The kthread name.
 * @return The kthread on success, or MR_NULL on failure.
 */
mr_kthread_t *mr_kthread_find(const char *name);

/**
 * @brief This function get a kthread.
 *
 * @param kth The kthread.
 * @return The kthread on success, or MR_NULL on failure.
 */
MR_INLINE mr_kthread_t *mr_kthread_get(mr_kthread_t *kth) {
    return (mr_kthread_t *)mr_kobject_get((mr_kobject_t *)kth);
}

/**
 * @brief This function put a kthread.
 *
 * @param kth The kthread.
 */
MR_INLINE void mr_kthread_put(mr_kthread_t *kth) {
    mr_kobject_put((mr_kobject_t *)kth);
}
#endif /* defined(MR_USE_KOS) */

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_KTHREAD_H__ */
