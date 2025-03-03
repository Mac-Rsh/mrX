/**
 * @copyright (c) 2024, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_KMUTEX_H__
#define __MR_KMUTEX_H__

#include <mr_config.h>
#if defined(MR_USE_KOS) && defined(MR_USE_KMUTEX)
#include <kernel/mr_kclock.h>
#include <kernel/mr_kobject.h>
#endif /* defined(MR_USE_KOS) && defined(MR_USE_KMUTEX) */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup Kmutex
 * @{
 */

#if defined(MR_USE_KOS) && defined(MR_USE_KMUTEX)
/* Kmutex type */
typedef struct mr_kmutex {
    struct mr_kobject parent;
    mr_ptr_t mutex;
#if !defined(MR_CFG_KMUTEX_IMUTEX_SIZE)
#define MR_CFG_KMUTEX_IMUTEX_SIZE       (64)
#endif /* !defined(MR_CFG_KMUTEX_IMUTEX_SIZE) */
    mr_uint8_t imutex[MR_CFG_KMUTEX_IMUTEX_SIZE];
} mr_kmutex_t;

/**
 * @brief This macro function checks if a kmutex is initialized.
 *
 * @param _kmutex The kmutex.
 * @return MR_TRUE if the kmutex is initialized, MR_FALSE otherwise.
 */
#define MR_KMUTEX_IS_INITED(_kmutex)    MR_KOBJECT_IS_INITED(_kmutex)

/**
 * @brief This function initializes a kmutex.
 *
 * @param kmutex The kmutex.
 * @param name The kmutex name.
 * @return 0 on success, or a negative error code on failure.
 */
mr_err_t mr_kmutex_init(mr_kmutex_t *kmutex, const char *name);

/**
 * @brief This function create a kmutex.
 *
 * @param name The kmutex name.
 * @return The kmutex on success, or MR_NULL on failure.
 */
mr_kmutex_t *mr_kmutex_create(const char *name);

/**
 * @brief This function delete a kmutex.
 *
 * @param kmutex The kmutex.
 * @return 0 on success, or a negative error code on failure.
 */
mr_err_t mr_kmutex_del(mr_kmutex_t *kmutex);

/**
 * @brief This function take a kmutex.
 *
 * @param kmutex The kmutex.
 * @param timeout The timeout(tick).
 * @return 0 on success, or a negative error code on failure.
 *
 * @note If timeout is '0', no waiting.
 * @note If timeout is '-1', waiting forever.
 */
mr_err_t mr_kmutex_take(mr_kmutex_t *kmutex, mr_tick_t timeout);

/**
 * @brief This function try to take a kmutex.
 *
 * @param kmutex The kmutex.
 * @return 0 on success, or a negative error code on failure.
 */
mr_err_t mr_kmutex_trytake(mr_kmutex_t *kmutex);

/**
 * @brief This function give a kmutex.
 *
 * @param kmutex The kmutex.
 * @return 0 on success, or a negative error code on failure.
 */
mr_err_t mr_kmutex_give(mr_kmutex_t *kmutex);

/**
 * @brief This function find a kmutex.
 *
 * @param name The kmutex name.
 * @return The kmutex on success, or MR_NULL on failure.
 */
mr_kmutex_t *mr_kmutex_find(const char *name);

/**
 * @brief This function get a kmutex.
 *
 * @param kmutex The kmutex.
 * @return The kmutex on success, or MR_NULL on failure.
 */
MR_INLINE mr_kmutex_t *mr_kmutex_get(mr_kmutex_t *kmutex) {
    return (mr_kmutex_t *)mr_kobject_get((mr_kobject_t *)kmutex);
}

/**
 * @brief This function put a kmutex.
 *
 * @param kmutex The kmutex.
 */
MR_INLINE void mr_kmutex_put(mr_kmutex_t *kmutex) {
    mr_kobject_put((mr_kobject_t *)kmutex);
}
#endif /* defined(MR_USE_KOS) && defined(MR_USE_KMUTEX) */

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_KMUTEX_H__ */
