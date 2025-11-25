/**
 * @copyright (c) 2024-2025, MacRsh
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

/* Slab bitmap size */
#define MR_SLAB_BITMAP_SIZE(_num)       (((_num) + 31) >> 5)

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
 * @param _buf The buffer.
 * @param _size The memory size.
 * @param _num The memory number.
 */
#define MR_SLAB_INIT(_buf, _size, _num)                                        \
    {.map = *MR_MAKE_LOCAL(mr_atomic_t[MR_SLAB_BITMAP_SIZE(_num)]),            \
     .buf = (mr_uint8_t *)(_buf),                                              \
     .size = (_size),                                                          \
     .num = (_num)}

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

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_SLAB_H__ */
