/**
 * @copyright (c) 2024, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_IRQ_H__
#define __MR_IRQ_H__

#include <mr_config.h>
#if defined(MR_USE_IRQ)
#include <kernel/mr_kworkqueue.h>
#endif /* defined(MR_USE_IRQ) */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup Irq
 * @{
 */

#if defined(MR_USE_IRQ)
/* Irq flag definition */
#define MR_IRQ_EDGE_RISING              (0x01)
#define MR_IRQ_EDGE_FALLING             (0x02)
#define MR_IRQ_EDGE_BOTH                (0x03)
#define MR_IRQ_LEVEL_HIGH               (0x04)
#define MR_IRQ_LEVEL_LOW                (0x08)
#define MR_IRQ_SHARED                   (0x10)

/* Irq return definition */
typedef enum mr_irq_return {
    MR_IRQ_NONE,
    MR_IRQ_HANDLED,
    MR_IRQ_DEFERRED,
} mr_irq_return_t;

/* Irq operations type */
typedef struct mr_irq_ops {
    void (*priority_set)(mr_uint32_t irq, mr_uint8_t priority, void *args);
    void (*type_set)(mr_uint32_t irq, mr_uint8_t type, void *args);
    void (*enable)(mr_uint32_t irq, void *args);
    void (*unmask)(mr_uint32_t irq, void *args);
    void (*disable)(mr_uint32_t irq, void *args);
    void (*mask)(mr_uint32_t irq, void *args);
} mr_irq_ops_t;

/* Irq type */
typedef struct mr_irq {
    mr_kobject_t parent;
    mr_uint32_t irq_start;
    mr_uint32_t irq_end;
    mr_atomic_t dying;
    mr_irq_ops_t *ops;
    mr_ptr_t args;
} mr_irq_t;

/* Irq action type */
typedef struct mr_irq_action {
    struct mr_irq_action *next;
    mr_uint32_t irq;
    mr_ptr_t entry;
    mr_ptr_t owner;
    mr_kwork_t defer;
    mr_ptr_t defer_entry;
} mr_irq_action_t;

/* Irq descriptor type */
typedef struct mr_irq_desc {
    mr_irq_action_t *action;
    mr_uint32_t priority;
    mr_uint32_t flags;
    mr_atomic_t depth;
    mr_irq_t *irq;
} mr_irq_desc_t;

/* Irq entry type */
typedef mr_irq_return_t(mr_irq_entry_t)(mr_uint32_t irq, void *owner);

#if defined(MR_USE_KWORKQUEUE_HOOK)
/* Irq hook type */
typedef void(mr_irq_hook_t)(void *args);
#endif /* defined(MR_USE_KWORKQUEUE_HOOK) */

/**
 * @brief This macro function checks if an irq is initialized.
 *
 * @param _irq The irq.
 * @return MR_TRUE if the irq is initialized, MR_FALSE otherwise.
 */
#define MR_IRQ_IS_INITED(_irq)          MR_KOBJECT_IS_INITED(_irq)

/**
 * @brief This function initializes an irq.
 *
 * @param irq The irq.
 * @param name The irq name.
 * @param irq_start The start irq number.
 * @param irq_end The end irq number.
 * @param ops The irq operations.
 * @param args The irq arguments.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_irq_init(mr_irq_t *irq, const char *name, mr_uint32_t irq_start,
                     mr_uint32_t irq_end, mr_irq_ops_t *ops, void *args);

/**
 * @brief This function creates an irq.
 *
 * @param name The irq name.
 * @param irq_start The start irq number.
 * @param irq_end The end irq number.
 * @param ops The irq operations.
 * @param args The irq arguments.
 * @return The irq on success, MR_NULL on failure.
 */
mr_irq_t *mr_irq_create(const char *name, mr_uint32_t irq_start,
                        mr_uint32_t irq_end, mr_irq_ops_t *ops, void *args);

/**
 * @brief This function deletes an irq.
 *
 * @param irq The irq.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_irq_del(mr_irq_t *irq);

/**
 * @brief This function requests an irq.
 *
 * @param irq The irq number.
 * @param entry The irq entry.
 * @param owner The irq owner.
 * @param flags The irq flags.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_irq_request(mr_uint32_t irq, mr_irq_entry_t *entry, void *owner,
                        mr_uint32_t flags);

/**
 * @brief This function requests an irq.
 *
 * @param irq The irq number.
 * @param entry The irq entry.
 * @param owner The irq owner.
 * @param flags The irq flags.
 * @param defer_entry The irq defer entry.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_irq_defer_request(mr_uint32_t irq, mr_irq_entry_t *entry,
                              void *owner, mr_uint32_t flags,
                              mr_irq_entry_t *defer_entry);

/**
 * @brief This function frees an irq.
 *
 * @param irq The irq number.
 * @param owner The irq owner.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_irq_free(mr_uint32_t irq, void *owner);

/**
 * @brief This function enables an irq.
 *
 * @param irq The irq number.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_irq_enable(mr_uint32_t irq);

/**
 * @brief This function disables an irq.
 *
 * @param irq The irq number.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_irq_disable(mr_uint32_t irq);

/**
 * @brief This function gets an irq type.
 *
 * @param irq The irq number.
 * @return The irq type on success, a negative error code on failure.
 */
int mr_irq_type(mr_uint32_t irq);

/**
 * @brief This function sets an irq priority.
 *
 * @param irq The irq number.
 * @param priority The irq priority(0: highest, 255: lowest).
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_irq_priority_set(mr_uint32_t irq, mr_uint8_t priority);

/**
 * @brief This function gets an irq priority.
 *
 * @param irq The irq number.
 * @return The irq priority on success, a negative error code on failure.
 */
int mr_irq_priority(mr_uint32_t irq);

/**
 * @brief This function handles an irq.
 *
 * @param irq The irq number.
 * @return The irq return on success, MR_IRQ_NONE on failure.
 */
mr_irq_return_t mr_irq_handle(mr_uint32_t irq);

/**
 * @brief This function execute deferred irq.
 *
 * @note User does not need to call this function.
 */
void mr_irq_defer_execute(void);

#if defined(MR_USE_KWORKQUEUE_HOOK)
/**
 * @brief This function set an irq defer wakeup hook.
 *
 * @param hook The irq defer wakeup hook.
 * @return 0 on success, a negative error code on failure.
 *
 * @note Use a semaphore with an initial value of 0 as the hook.
 */
mr_err_t mr_irq_defer_hook_wakeup_set(mr_irq_hook_t *hook);

/**
 * @brief This function set an irq defer suspend hook.
 *
 * @param hook The irq defer suspend hook.
 * @return 0 on success, a negative error code on failure.
 *
 * @note Use a semaphore with an initial value of 0 as the hook.
 */
mr_err_t mr_irq_defer_hook_suspend_set(mr_irq_hook_t *hook);

/**
 * @brief This function set an irq defer hook arguments.
 *
 * @param args The irq defer hook arguments.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_irq_defer_hook_args_set(void *args);
#endif /* defined(MR_USE_KWORKQUEUE_HOOK) */

/**
 * @brief This function find an irq.
 *
 * @param name The irq name.
 * @return The irq on success, MR_NULL on failure.
 */
mr_irq_t *mr_irq_find(const char *name);

/**
 * @brief This function get an irq.
 *
 * @param irq The irq.
 * @return The irq on success, MR_NULL on failure.
 */
MR_INLINE mr_irq_t *mr_irq_get(mr_irq_t *irq) {
    return (mr_irq_t *)mr_kobject_get((mr_kobject_t *)irq);
}

/**
 * @brief This function put an irq.
 *
 * @param irq The irq.
 */
MR_INLINE void mr_irq_put(mr_irq_t *irq) {
    mr_kobject_put((mr_kobject_t *)irq);
}
#endif /* defined(MR_USE_IRQ) */

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_IRQ_H__ */
