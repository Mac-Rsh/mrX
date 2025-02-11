/**
 * @copyright (c) 2024, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_KOS_H__
#define __MR_KOS_H__

#include <kernel/mr_kclock.h>
#include <kernel/mr_kobject.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup Kos
 * @{
 */

/* Kthread operations type */
typedef struct mr_kthread_ops {
    mr_err_t (*init)(void *, const char *, void *, void *, mr_size_t,
                     mr_uint32_t, mr_tick_t);
    mr_err_t (*del)(void *);
    mr_err_t (*startup)(void *);
    mr_err_t (*resume)(void *);
    mr_err_t (*yield)(void);
    mr_err_t (*suspend)(void *);
    mr_err_t (*sleep)(mr_tick_t tick);
    mr_err_t (*exit)(void *);
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
 * @brief This function registers kernel os.
 *
 * @return 0 on success, or a negative error code on failure.
 */
mr_err_t mr_kos_register(mr_kos_type_t *type);

/**
 * @brief This function gets kernel os.
 *
 * @return The kernel os.
 */
mr_kos_t *mr_kos_get(void);

/**
 * @brief This function puts kernel os.
 */
void mr_kos_put(void);

/**
 * @brief This macro function gets the kthread operations of a kernel os.
 *
 * @param _kos The kernel os.
 * @return The kthread operations of the kernel os.
 */
#define MR_KOS_KTH_OPS(_kos)            ((_kos)->type->kth_type->ops)

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_KOS_H__ */
