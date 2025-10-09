/**
 * @copyright (c) 2024-2025, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_LIBC_MALLOC_H__
#define __MR_LIBC_MALLOC_H__

#include <mr_config.h>
#if defined(MR_USE_MALLOC_STD)
#include <stdlib.h>
#else
#include <libc/mr_compiler.h>
#include <libc/mr_types.h>
#endif /* defined(MR_USE_MALLOC_STD) */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup Malloc
 * @{
 */

/* Malloc definition */
#if defined(MR_USE_MALLOC_STD)
#define mr_malloc(_sz)                  malloc(_sz)
#define mr_free(_m)                     free(_m)
#define mr_calloc(_n, _sz)              calloc(_n, _sz)
#define mr_realloc(_m, _sz)             realloc(_m, _sz)
#else
#if defined(MR_USE_MALLOC)
/* Heap magic definition */
#define MR_HEAP_MAGIC                   (0x6d725800U)

/* Heap block type */
typedef struct mr_heap_block {
    volatile mr_uint32_t magic;
    struct mr_heap_block *next;
    mr_size_t size;
} mr_heap_block_t;

/* Heap region type */
typedef struct mr_heap_region {
    void *address;
    mr_size_t size;
} mr_heap_region_t;
#endif /* defined(MR_USE_MALLOC) */

/**
 * @brief This function allocates memory.
 *
 * @param size The allocated size.
 * @return The allocated memory.
 */
void *mr_malloc(mr_size_t size);

/**
 * @brief This function free memory.
 *
 * @param mem The free memory.
 */
void mr_free(void *mem);

/**
 * @brief This function returns the usable size of memory.
 *
 * @param mem The memory.
 * @return The usable size.
 */
mr_size_t mr_malloc_usable_size(void *mem);

/**
 * @brief This function allocates memory.
 *
 * @param num The allocated block number.
 * @param size The allocated block size.
 * @return The allocated memory.
 */
void *mr_calloc(mr_size_t num, mr_size_t size);

/**
 * @brief This function reallocates memory.
 *
 * @param mem The memory.
 * @param size The reallocated size.
 * @return The reallocated memory.
 */
void *mr_realloc(void *mem, mr_size_t size);

/**
 * @brief This function adds memory regions.
 *
 * @param regions The memory regions.
 *
 * @note The last region address must be MR_NULL.
 */
void mr_heap_region_add(const mr_heap_region_t *regions);
#endif /* defined(MR_USE_MALLOC_STD) */

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_LIBC_MALLOC_H__ */
