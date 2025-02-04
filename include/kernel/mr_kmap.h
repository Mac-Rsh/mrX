/**
 * @copyright (c) 2024, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_KMAP_H__
#define __MR_KMAP_H__

#include <libc/mr_compiler.h>
#include <libc/mr_errno.h>
#include <libc/mr_types.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup Kmap
 * @{
 */

/* Knode type */
typedef struct mr_knode {
    mr_uint32_t key;
    void *value;
    struct mr_knode *next;
} mr_knode_t;

/* Kmap type */
typedef struct mr_kmap {
    mr_knode_t **b;
    mr_size_t size;
    mr_size_t count;
    mr_f32_t load_factor;
} mr_kmap_t;

/**
 * @brief This function initializes a kmap.
 *
 * @param kmap The kmap to initialize.
 * @param size The size of the kmap.
 * @param load_factor The load factor of the kmap.
 * @return 0 on success, or a negative error code on failure.
 */
mr_err_t mr_kmap_init(mr_kmap_t *kmap, mr_size_t size, mr_f32_t load_factor);

/**
 * @brief This function adds a key-value pair to the kmap.
 *
 * @param kmap The kmap to add the key-value pair to.
 * @param key The key to add.
 * @param value The value to add.
 * @return 0 on success, or a negative error code on failure.
 */
mr_err_t mr_kmap_add(mr_kmap_t *kmap, mr_uint32_t key, void *value);

/**
 * @brief This function deletes a key-value pair from the kmap.
 *
 * @param kmap The kmap to delete the key-value pair from.
 * @param key The key to delete.
 * @return The value of the deleted key-value pair, or MR_NULL if not found.
 */
void *mr_kmap_del(mr_kmap_t *kmap, mr_uint32_t key);

/**
 * @brief This function gets the value of a key from the kmap.
 *
 * @param kmap The kmap to get the value from.
 * @param key The key to get the value of.
 * @return The value of the key, or MR_NULL if not found.
 */
void *mr_kmap_value(mr_kmap_t *kmap, mr_uint32_t key);

/**
 * @brief This function clears the kmap.
 *
 * @param kmap The kmap to clear.
 */
void mr_kmap_clear(mr_kmap_t *kmap);

/**
 * @brief This function shrinks the kmap.
 *
 * @param kmap The kmap to shrink.
 * @param size The new size of the kmap.
 * @return 0 on success, or a negative error code on failure.
 */
mr_err_t mr_kmap_shrink(mr_kmap_t *kmap, mr_size_t size);

/**
 * @brief This function gets the size of the kmap.
 *
 * @param kmap The kmap to get the size of.
 * @return The size of the kmap.
 */
MR_INLINE mr_size_t mr_kmap_count(mr_kmap_t *kmap) {
    return kmap->count;
}

/**
 * @brief This function checks if the kmap is empty.
 *
 * @param kmap The kmap to check.
 * @return MR_TRUE if the kmap is empty, MR_FALSE otherwise.
 */
MR_INLINE mr_bool_t mr_kmap_is_empty(mr_kmap_t *kmap) {
    return (kmap->count == 0) ? MR_TRUE : MR_FALSE;
}

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_KMAP_H__ */
