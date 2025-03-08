/**
 * @copyright (c) 2024, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_MALLOC_H__
#define __MR_MALLOC_H__

#include <mr_config.h>
#if defined(MR_USE_LIBC_MALLOC)
#include <stdlib.h>
#else
#include <libc/mr_types.h>
#endif /* defined(MR_USE_LIBC_MALLOC) */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup Malloc
 * @{
 */

/* Malloc definition */
#if defined(MR_USE_LIBC_MALLOC)
#define mr_malloc(_sz)                  malloc(_sz)
#define mr_free(_m)                     free(_m)
#define mr_malloc_usable_size(_m)       malloc_usable_size(_m)
#define mr_calloc(_n, _sz)              calloc(_n, _sz)
#define mr_realloc(_m, _sz)             realloc(_m, _sz)
#else
#if !defined(MR_USE_3PARTY_MALLOC)
/* Memory block type */
typedef struct mr_memblk {
    struct mr_memblk *next;
    mr_uint32_t magic;
    mr_size_t size;
} mr_memblk_t;
#endif /* !defined(MR_USE_3PARTY_MALLOC) */

/**
 * @brief This function allocates memory.
 *
 * @param size The size of memory to allocate.
 * @return The allocated memory.
 */
void *mr_malloc(mr_size_t size);

/**
 * @brief This function free mem.
 *
 * @param mem The mem to free.
 */
void mr_free(void *mem);

/**
 * @brief This function returns the usable size of mem.
 *
 * @param mem The mem.
 * @return The usable size of mem.
 */
mr_size_t mr_malloc_usable_size(void *mem);

/**
 * @brief This function allocates memory.
 *
 * @param num The number of blocks to allocate.
 * @param size The size of each block.
 * @return The allocated memory.
 */
void *mr_calloc(mr_size_t num, mr_size_t size);

/**
 * @brief This function reallocates mem.
 *
 * @param mem The mem to reallocate.
 * @param size The new size of mem.
 * @return The reallocated mem.
 */
void *mr_realloc(void *mem, mr_size_t size);
#endif /* defined(MR_USE_LIBC_MALLOC) */

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_MALLOC_H__ */
