/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_IRQ_H__
#define __MR_IRQ_H__

#include <mrX/mr_config.h>
#if defined(MR_USE_IRQ)
#if defined(MR_USE_IRQ_DEFER)
#include <mrX/kernel/mr_work.h>
#else
#include <mrX/kernel/mr_object.h>
#endif /* defined(MR_USE_IRQ_DEFER) */
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
#define MR_IRQ_EDGE_RISING              (0x01U)
#define MR_IRQ_EDGE_FALLING             (0x02U)
#define MR_IRQ_EDGE_BOTH                (0x03U)
#define MR_IRQ_LEVEL_HIGH               (0x04U)
#define MR_IRQ_LEVEL_LOW                (0x08U)
#define MR_IRQ_SHARED                   (0x10U)

/* Irq return definition */
typedef enum mr_irq_return {
    MR_IRQ_NONE,
    MR_IRQ_HANDLED,
    MR_IRQ_DEFERRED,
} mr_irq_return_t;

/* Irq operations type */
typedef struct mr_irq_ops {
    void (*priority_set)(mr_uint32_t irq, mr_uint8_t priority, void *priv);
    void (*type_set)(mr_uint32_t irq, mr_uint8_t type, void *priv);
    void (*enable)(mr_uint32_t irq, void *priv);
    void (*unmask)(mr_uint32_t irq, void *priv);
    void (*disable)(mr_uint32_t irq, void *priv);
    void (*mask)(mr_uint32_t irq, void *priv);
} mr_irq_ops_t;

#if defined(MR_USE_IRQ_DEFER_HOOK)
/* Irq hook type */
typedef struct mr_irq_defer_hook {
    void (*suspend)(void *param);
    void (*wakeup)(void *param);
    void *param;
} mr_irq_defer_hook_t;
#endif /* defined(MR_USE_IRQ_DEFER_HOOK) */

/* Irq type */
MR_CLAZZES_EXPORT(irq);
typedef struct mr_irq {
    mr_object_t parent;
    mr_atomic_t dying;
    mr_uint32_t istart;
    mr_uint32_t iend;
    const mr_irq_ops_t *ops;
    mr_ptr_t priv;
} mr_irq_t;

/* Irq action type */
typedef struct mr_irq_action {
    struct mr_irq_action *next;
    mr_uint32_t pending;
    mr_uint32_t irq;
    mr_ptr_t entry;
    mr_ptr_t owner;
#if defined(MR_USE_IRQ_DEFER)
    mr_work_t defer;
    mr_ptr_t defer_entry;
#endif /* defined(MR_USE_IRQ_DEFER) */
} mr_irq_action_t;

/* Irq descriptor type */
typedef struct mr_irq_desc {
    mr_irq_action_t *action;
    mr_uint16_t priority;
    mr_uint16_t flags;
    mr_atomic_t depth;
    mr_irq_t *irq;
} mr_irq_desc_t;

/* Irq entry type */
typedef mr_irq_return_t(mr_irq_entry_t)(mr_uint32_t irq, void *owner);

/**
 * @brief This function initializes an irq.
 *
 * @param irq The irq.
 * @param irq_start The start irq number.
 * @param irq_end The end irq number.
 * @param ops The irq operations.
 * @param priv The irq private data.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_irq_init(mr_irq_t *irq, mr_uint32_t irq_start, mr_uint32_t irq_end,
                     const mr_irq_ops_t *ops, void *priv);

/**
 * @brief This function creates an irq.
 *
 * @param irq_start The start irq number.
 * @param irq_end The end irq number.
 * @param ops The irq operations.
 * @param priv The irq private data.
 * @return The irq on success, MR_NULL on failure.
 */
mr_irq_t *mr_irq_create(mr_uint32_t irq_start, mr_uint32_t irq_end,
                        const mr_irq_ops_t *ops, void *priv);

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
                        mr_uint16_t flags);

#if defined(MR_USE_IRQ_DEFER)
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
                              void *owner, mr_uint16_t flags,
                              mr_irq_entry_t *defer_entry);
#endif /* defined(MR_USE_IRQ_DEFER) */

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

#if defined(MR_USE_IRQ_DEFER)
/**
 * @brief This function execute deferred irq.
 *
 * @note User does not need to call this function.
 */
void mr_irq_defer_execute(void);
#endif /* defined(MR_USE_IRQ_DEFER) */

/**
 * @brief This function get an irq.
 *
 * @param irq The irq.
 * @return The irq on success, MR_NULL on failure.
 */
MR_INLINE mr_irq_t *mr_irq_get(mr_irq_t *irq) {
    /* Get irq reference */
    return (mr_irq_t *)mr_object_get((mr_object_t *)irq);
}

/**
 * @brief This function put an irq.
 *
 * @param irq The irq.
 * @return MR_TRUE on end of life cycle, MR_FALSE otherwise.
 */
MR_INLINE mr_bool_t mr_irq_put(mr_irq_t *irq) {
    /* Put irq reference */
    return mr_object_put((mr_object_t *)irq);
}

#if defined(MR_USE_IRQ_DEFER_HOOK)
/**
 * @brief This function set an irq defer hook.
 *
 * @param hook The irq defer hook.
 * @return 0 on success, a negative error code on failure.
 *
 * @note Recommend to use a semaphore with an initial value of 0 as the hook.
 */
mr_err_t mr_irq_defer_hook_set(const mr_irq_defer_hook_t *hook);
#endif /* defined(MR_USE_IRQ_DEFER_HOOK) */
#endif /* defined(MR_USE_IRQ) */

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_IRQ_H__ */
