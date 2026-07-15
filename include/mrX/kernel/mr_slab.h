/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_SLAB_H__
#define __MR_SLAB_H__

#include <mrX/kernel/mr_service.h>
#include <mrX/libc/mr_atomic.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup Slab
 * @{
 */

/* Slab bitmap bits */
#define MR_SLAB_MAP_BITS                                                       \
    ((mr_uint32_t)(sizeof(mr_atomic_t) * 8U))

/* Slab bitmap size */
#define MR_SLAB_MAP_SIZE(_num)                                                 \
    ((((_num) / MR_SLAB_MAP_BITS) + (((_num) % MR_SLAB_MAP_BITS) != 0U))       \
     * sizeof(mr_atomic_t))

/* Slab type */
typedef struct mr_slab {
    mr_atomic_t *map;
    mr_uint32_t size;
    mr_uint8_t *buf;
    mr_uint32_t num;
} mr_slab_t;

/**
 * @brief This macro function initializes a slab.
 *
 * @param _map The bitmap(satisfy the 'MR_SLAB_MAP_SIZE').
 * @param _buf The buffer.
 * @param _size The memory size.
 * @param _num The memory number.
 */
#define MR_SLAB_INIT(_map, _buf, _size, _num)                                  \
    {                                                                          \
        .map = (mr_atomic_t *)(_map),                                          \
        .buf = (mr_uint8_t *)(_buf),                                           \
        .size = (_size),                                                       \
        .num = (_num),                                                         \
    }

/**
 * @brief This function initializes a slab.
 *
 * @param slab The slab.
 * @param map The bitmap.
 * @param map_size The bitmap size.
 * @param buf The buffer.
 * @param size The memory size.
 * @param num The memory number.
 * @return The memory number.
 */
MR_INLINE mr_uint32_t mr_slab_init(mr_slab_t *slab, void *map,
                                   mr_uint32_t map_size, void *buf,
                                   mr_uint32_t size, mr_uint32_t num) {
    mr_uint32_t i;

    /* Check parameter */
    if (((mr_uintptr_t)map % sizeof(mr_atomic_t)) != 0U) {
        /* Align error */
        return 0;
    }

    /* Limit num */
    map_size /= sizeof(mr_atomic_t);
    num = MR_MIN(num, map_size * MR_SLAB_MAP_BITS);

    /* Init slab map */
    for (i = 0; i < map_size; i++) {
        mr_atomic_init(&((mr_atomic_t *)map)[i], 0);
    }

    /* Init slab */
    slab->map = (mr_atomic_t *)map;
    slab->buf = (mr_uint8_t *)buf;
    slab->size = size;
    slab->num = num;
    return num;
}

/**
 * @brief This function allocates a memory from slab.
 *
 * @param slab The slab.
 * @return The allocated memory.
 */
void *mr_slab_alloc(mr_slab_t *slab);

/**
 * @brief This function free a memory to slab.
 *
 * @param slab The slab.
 * @param mem The memory.
 */
void mr_slab_free(mr_slab_t *slab, void *mem);

/**
 * @brief This function converts a memory pointer to a memory index.
 *
 * @param slab The slab.
 * @param mem The memory.
 * @return The memory index.
 */
MR_INLINE int mr_slab_ptr2index(mr_slab_t *slab, void *mem) {
    mr_uint32_t off;

    /* Check parameter */
    if ((mem >= (void *)(slab->buf + (slab->num * slab->size)))
        || (mem < (void *)slab->buf)) {
        return -1;
    }

    /* Get memory offset */
    off = (mr_uint32_t)((mr_uint8_t *)mem - slab->buf);
    if ((off % slab->size) != 0U) {
        /* Memory is not an object start */
        return -1;
    }

    /* Return memory index */
    return (int)(off / slab->size);
}

/**
 * @brief This function converts a memory index to a memory pointer.
 *
 * @param slab The slab.
 * @param index The memory index.
 * @return The memory pointer.
 */
MR_INLINE void *mr_slab_index2ptr(mr_slab_t *slab, int index) {
    /* Check parameter */
    if ((index < 0) || (index >= (int)slab->num)) {
        return MR_NULL;
    }

    /* Return memory */
    return slab->buf + ((mr_uint32_t)index * slab->size);
}

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_SLAB_H__ */
