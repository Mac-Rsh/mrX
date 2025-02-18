/**
 * @copyright (c) 2024, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_KOS_H__
#define __MR_KOS_H__

#include <mr_config.h>
#if defined(MR_USE_KOS)
#include <kernel/mr_kclock.h>
#include <kernel/mr_kobject.h>
#endif /* defined(MR_USE_KOS) */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup Kos
 * @{
 */

#if defined(MR_USE_KOS)
/* Kthread operations type */
typedef struct mr_kthread_ops {
    mr_err_t (*init)(void *res, const char *name, void *kth, void *stack,
                     mr_size_t stack_size, mr_uint32_t priority,
                     mr_tick_t tick);
    mr_err_t (*del)(void *res);
    mr_err_t (*startup)(void *res);
    mr_err_t (*resume)(void *res);
    mr_err_t (*yield)(void);
    mr_err_t (*suspend)(void *res);
    mr_err_t (*sleep)(mr_tick_t tick);
    mr_err_t (*exit)(void *res);
    void *(*self)(void);
} mr_kthread_ops_t;

/* Kthread-type type */
typedef struct mr_kthread_type {
    mr_uint32_t priority_max;
    mr_kthread_ops_t *ops;
    mr_size_t size;
} mr_kthread_type_t;

/* Kos-type type */
typedef struct mr_kos_type {
    mr_kthread_type_t *kth_type;
} mr_kos_type_t;

/* Kos type */
typedef struct mr_kos {
    mr_kset_t parent;
    mr_kos_type_t *type;
} mr_kos_t;

/**
 * @brief This function registers a kernel os.
 *
 * @return 0 on success, or a negative error code on failure.
 */
mr_err_t mr_kos_register(mr_kos_type_t *ktype);

/**
 * @brief This function gets a kernel os.
 *
 * @param kos The kernel os.
 * @return The kernel os on success, or MR_NULL on failure.
 */
MR_INLINE mr_kos_t *mr_kos_get(mr_kos_t *kos) {
    return (mr_kos_t *)mr_kobject_get((mr_kobject_t *)kos);
}

/**
 * @brief This function puts a kernel os.
 *
 * @param kos The kernel os.
 */
MR_INLINE void mr_kos_put(mr_kos_t *kos) {
    mr_kobject_put((mr_kobject_t *)kos);
}

/**
 * @brief This function finds a kernel os.
 *
 * @return The kernel os.
 */
mr_kos_t *mr_kos_find(void);
#endif /* defined(MR_USE_KOS) */

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_KOS_H__ */
